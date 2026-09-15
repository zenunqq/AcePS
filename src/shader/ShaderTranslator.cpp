/*
 * ShaderTranslator.cpp implements a conservative GCN-to-SPIR-V translator.
 * Unknown instructions are skipped, while known arithmetic, memory, texture,
 * and export forms become real SPIR-V operations in a valid minimal module.
 */
#include "aceps/shader/ShaderTranslator.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace aceps::shader {
namespace {

constexpr std::uint32_t kSpirvMagic = 0x07230203U;
constexpr std::uint32_t kVersion = 0x00010500U;
constexpr std::uint32_t kGenerator = 0;

enum Op : std::uint16_t {
  OpName = 5, OpExtInstImport = 11, OpExtInst = 12, OpMemoryModel = 14,
  OpEntryPoint = 15, OpExecutionMode = 16, OpCapability = 17,
  OpTypeVoid = 19, OpTypeBool = 20, OpTypeInt = 21, OpTypeFloat = 22,
  OpTypeVector = 23, OpTypeImage = 25, OpTypeSampledImage = 86,
  OpTypeArray = 28, OpTypeStruct = 30, OpTypePointer = 32,
  OpTypeFunction = 33, OpConstant = 43, OpFunction = 54, OpFunctionEnd = 56,
  OpVariable = 59, OpLoad = 61, OpStore = 62, OpAccessChain = 65,
  OpDecorate = 71, OpMemberDecorate = 72, OpCompositeConstruct = 80,
  OpCompositeExtract = 81, OpCopyObject = 83,
  OpImageSampleImplicitLod = 87, OpConvertFToU = 109, OpConvertFToS = 110,
  OpConvertUToF = 114, OpConvertSToF = 115, OpFDiv = 136, OpFMod = 141,
  OpIAdd = 128,
  OpShiftRightLogical = 194, OpShiftRightArithmetic = 195,
  OpShiftLeftLogical = 196, OpBitwiseOr = 197, OpBitwiseXor = 198,
  OpBitwiseAnd = 199, OpIMul = 132, OpFAdd = 129, OpISub = 130,
  OpFSub = 131, OpFMul = 133, OpFma = 319,
  OpFOrdGreaterThan = 170, OpFNegate = 127, OpBranch = 249,
  OpBranchConditional = 250, OpReturn = 253, OpLabel = 248,
};

constexpr std::uint32_t kCapabilityShader = 1;
constexpr std::uint32_t kAddressingLogical = 0;
constexpr std::uint32_t kMemoryModelGLSL450 = 1;
constexpr std::uint32_t kExecutionModelVertex = 0;
constexpr std::uint32_t kExecutionModelFragment = 4;
constexpr std::uint32_t kExecutionModelGLCompute = 5;
constexpr std::uint32_t kExecutionModeLocalSize = 17;
constexpr std::uint32_t kExecutionModeOriginUpperLeft = 7;
constexpr std::uint32_t kStorageUniformConstant = 0;
constexpr std::uint32_t kStorageUniform = 2;
constexpr std::uint32_t kStorageFunction = 7;
constexpr std::uint32_t kStorageOutput = 3;
constexpr std::uint32_t kDecorationBinding = 33;
constexpr std::uint32_t kDecorationDescriptorSet = 34;
constexpr std::uint32_t kDecorationLocation = 30;
constexpr std::uint32_t kDecorationBuiltIn = 11;
constexpr std::uint32_t kBuiltInPosition = 0;
constexpr std::uint32_t kDecorationBlock = 2;
constexpr std::uint32_t kDecorationOffset = 35;

struct Instruction final {
  enum class Format : std::uint8_t {
    Unknown, Sop2, Sopc, Sopp, Vop1, Vop2, Vop3, Vopc, Smem, Vmem, Exp
  };
  Format format{Format::Unknown};
  std::uint32_t word{0};
  std::uint64_t wideWord{0};
  std::uint32_t opcode{0};
  std::uint32_t destination{0};
  std::uint32_t source0{0};
  std::uint32_t source1{0};
  std::uint32_t vsrc2{0};
  std::uint32_t vsrc3{0};
  std::uint32_t expTarget{0};
  std::uint32_t expEn{0};
  bool expCompr{false};
};

Instruction decode(std::uint32_t word) noexcept {
  Instruction instruction;
  instruction.word = word;
  instruction.wideWord = word;
  instruction.destination = (word >> 18U) & 0xFFU;
  instruction.source0 = word & 0x1FFU;
  instruction.source1 = (word >> 9U) & 0x1FFU;
  const auto top9 = (word >> 23U) & 0x1FFU;
  if (top9 == 0x17EU) instruction.format = Instruction::Format::Sopc;
  else if (top9 == 0x17FU) instruction.format = Instruction::Format::Sopp;
  else if (((word >> 27U) & 0x1FU) == 0x18U) instruction.format = Instruction::Format::Smem;
  else if (((word >> 26U) & 0x3FU) == 0x38U) instruction.format = Instruction::Format::Vmem;
  else if (((word >> 26U) & 0x3FU) == 0x3EU) instruction.format = Instruction::Format::Exp;
  else if (top9 == 0x0FEU) instruction.format = Instruction::Format::Vop1;
  else if (((word >> 26U) & 0x3FU) == 0x34U) instruction.format = Instruction::Format::Vop3;
  else if (((word >> 30U) & 0x3U) == 0x2U) instruction.format = Instruction::Format::Sop2;
  else if ((word & 0x80000000U) == 0) instruction.format = Instruction::Format::Vop2;
  instruction.opcode = (word >> 16U) & 0xFFU;
  if (instruction.format == Instruction::Format::Exp) {
    instruction.source0 = (word >> 8U) & 0xFU;
    instruction.source1 = (word >> 12U) & 0xFU;
    instruction.vsrc2 = (word >> 16U) & 0xFU;
    instruction.vsrc3 = (word >> 20U) & 0xFU;
    instruction.expCompr = ((word >> 14U) & 0x1U) != 0U;
    instruction.expEn = word & 0x7FU;
  } else if (instruction.format == Instruction::Format::Vop3) {
    instruction.destination = (word >> 17U) & 0xFFU;
    instruction.source0 = word & 0x1FFU;
    instruction.source1 = (word >> 9U) & 0x1FFU;
    instruction.opcode = (word >> 16U) & 0xFFU;
  }
  return instruction;
}

void appendString(std::vector<std::uint32_t>& words, const char* text) {
  const auto length = std::strlen(text) + 1U;
  const auto wordCount = (length + 3U) / 4U;
  const auto start = words.size();
  words.resize(start + wordCount, 0);
  std::memcpy(words.data() + start, text, length);
}

void emit(std::vector<std::uint32_t>& words, Op op, std::initializer_list<std::uint32_t> operands) {
  words.push_back((static_cast<std::uint32_t>(operands.size()) + 1U) << 16U |
                  static_cast<std::uint16_t>(op));
  words.insert(words.end(), operands.begin(), operands.end());
}

void emitString(std::vector<std::uint32_t>& words, Op op,
                std::initializer_list<std::uint32_t> operands, const char* text) {
  words.push_back((static_cast<std::uint32_t>(operands.size()) + 1U +
                   static_cast<std::uint32_t>((std::strlen(text) + 4U) / 4U)) << 16U |
                  static_cast<std::uint16_t>(op));
  words.insert(words.end(), operands.begin(), operands.end());
  appendString(words, text);
}

class Emitter final {
public:
  Emitter(std::span<const std::uint32_t> bytecode, ShaderType hint)
      : bytecode_(bytecode), hint_(hint) {}

  bool run(std::vector<std::uint32_t>& output, std::string& error) {
    if (bytecode_.empty()) {
      error = "GCN shader bytecode is empty";
      return false;
    }
    for (std::size_t index = 0; index < bytecode_.size(); ++index) {
      auto instruction = decode(bytecode_[index]);
      instruction.wideWord = instruction.word;
      if ((instruction.format == Instruction::Format::Smem ||
           instruction.format == Instruction::Format::Vmem ||
           instruction.format == Instruction::Format::Exp ||
           instruction.format == Instruction::Format::Vop3) &&
          index + 1 < bytecode_.size()) {
        instruction.wideWord |= static_cast<std::uint64_t>(bytecode_[++index]) << 32U;
      }
      if (instruction.format == Instruction::Format::Vop3) {
        instruction.vsrc2 = static_cast<std::uint32_t>((instruction.wideWord >> 32U) & 0x1FFU);
      }
      if (instruction.format == Instruction::Format::Exp) {
        instruction.expTarget = static_cast<std::uint32_t>((instruction.wideWord >> 40U) & 0x3FU);
        hasMrt_ = hasMrt_ || instruction.expTarget <= 7U;
        hasPosition_ = hasPosition_ || instruction.expTarget == 12U;
      } else if (instruction.format == Instruction::Format::Vmem &&
                 ((instruction.wideWord >> 54U) & 0x1U) != 0U) {
        sampleNeeded_ = true;
      }
      instructions_.push_back(instruction);
    }
    type_ = hint_ == ShaderType::Auto ? (hasPosition_ ? ShaderType::Vertex :
                                          hasMrt_ ? ShaderType::Fragment : ShaderType::Compute) : hint_;
    if (type_ == ShaderType::Auto) type_ = ShaderType::Compute;
    buildHeader();
    emitBody();
    output = std::move(words_);
    error.clear();
    return true;
  }

private:
  std::uint32_t id() noexcept { return nextId_++; }

  void buildHeader() {
    words_ = {kSpirvMagic, kVersion, kGenerator, 0, 0};
    emit(words_, OpCapability, {kCapabilityShader});
    emit(words_, OpMemoryModel, {kAddressingLogical, kMemoryModelGLSL450});
    emit(words_, OpTypeVoid, {1});
    emit(words_, OpTypeBool, {2});
    emit(words_, OpTypeInt, {3, 32, 1});
    emit(words_, OpTypeFloat, {4, 32});
    emit(words_, OpTypeVector, {5, 4, 4});
    emit(words_, OpTypePointer, {6, kStorageFunction, 3});
    emit(words_, OpTypePointer, {7, kStorageFunction, 4});
    emit(words_, OpTypePointer, {8, kStorageOutput, 5});
    emit(words_, OpTypeFunction, {9, 1});
    emit(words_, OpConstant, {4, 10, 0});
    emit(words_, OpConstant, {4, 11, 0x3F800000U});

    if (type_ == ShaderType::Fragment) {
      emit(words_, OpVariable, {8, 12, kStorageOutput});
      emit(words_, OpDecorate, {12, kDecorationLocation, 0});
    } else if (type_ == ShaderType::Vertex) {
      emit(words_, OpVariable, {8, 15, kStorageOutput});
      emit(words_, OpDecorate, {15, kDecorationBuiltIn, kBuiltInPosition});
    }

    idGlsl_ = id();
    emitString(words_, OpExtInstImport, {idGlsl_}, "GLSL.std.450");

    const auto uboLength = id();
    emit(words_, OpConstant, {3, uboLength, 256});
    typeUboArray_ = id();
    emit(words_, static_cast<Op>(28), {typeUboArray_, 4, uboLength});
    typeUbo_ = id();
    emit(words_, static_cast<Op>(30), {typeUbo_, typeUboArray_});
    ptrUbo_ = id();
    emit(words_, OpTypePointer, {ptrUbo_, kStorageUniform, typeUbo_});
    uboVar_ = id();
    emit(words_, OpVariable, {ptrUbo_, uboVar_, kStorageUniform});
    emit(words_, OpDecorate, {uboVar_, kDecorationDescriptorSet, 0});
    emit(words_, OpDecorate, {uboVar_, kDecorationBinding, 0});
    emit(words_, OpDecorate, {typeUbo_, kDecorationBlock});
    emit(words_, OpMemberDecorate, {typeUbo_, 0, kDecorationOffset, 0});
    ptrUboFloat_ = id();
    emit(words_, OpTypePointer, {ptrUboFloat_, kStorageUniform, 4});
    ptrUboFloat_declared_ = true;

    if (sampleNeeded_) {
      typeImage_ = id();
      typeSampledImage_ = id();
      emit(words_, OpTypeImage, {typeImage_, 4, 1, 0, 0, 0, 1, 0});
      emit(words_, OpTypeSampledImage, {typeSampledImage_, typeImage_});
      ptrSampledImage_ = id();
      emit(words_, OpTypePointer, {ptrSampledImage_, kStorageUniformConstant, typeSampledImage_});
      sampledImageVar_ = id();
      emit(words_, OpVariable, {ptrSampledImage_, sampledImageVar_, kStorageUniformConstant});
      emit(words_, OpDecorate, {sampledImageVar_, kDecorationDescriptorSet, 1});
      emit(words_, OpDecorate, {sampledImageVar_, kDecorationBinding, 1});
      typeVec2_ = id();
      emit(words_, OpTypeVector, {typeVec2_, 4, 2});
      imageDeclared_ = true;
      vec2Declared_ = true;
    }

    const auto model = type_ == ShaderType::Vertex ? kExecutionModelVertex :
                       type_ == ShaderType::Fragment ? kExecutionModelFragment : kExecutionModelGLCompute;
    if (type_ == ShaderType::Fragment) emit(words_, OpExecutionMode, {13, kExecutionModeOriginUpperLeft});
    if (type_ == ShaderType::Compute) emit(words_, OpExecutionMode, {13, kExecutionModeLocalSize, 1, 1, 1});
    if (type_ == ShaderType::Vertex) emitString(words_, OpEntryPoint, {model, 13, 15}, "main");
    else if (type_ == ShaderType::Fragment) emitString(words_, OpEntryPoint, {model, 13, 12}, "main");
    else emitString(words_, OpEntryPoint, {model, 13}, "main");
  }

  std::uint32_t sgpr(std::uint32_t index) {
    index %= 104U;
    if (const auto found = sgprs_.find(index); found != sgprs_.end()) return found->second;
    const auto variable = id();
    emit(words_, OpVariable, {6, variable, kStorageFunction});
    sgprs_[index] = variable;
    return variable;
  }

  std::uint32_t vgpr(std::uint32_t index) {
    index %= 256U;
    if (const auto found = vgprs_.find(index); found != vgprs_.end()) return found->second;
    const auto variable = id();
    emit(words_, OpVariable, {7, variable, kStorageFunction});
    vgprs_[index] = variable;
    return variable;
  }

  std::uint32_t load(std::uint32_t type, std::uint32_t variable) {
    const auto result = id();
    emit(words_, OpLoad, {type, result, variable});
    return result;
  }

  void emitBody() {
    emit(words_, OpFunction, {1, 13, 0, 9});
    emit(words_, OpLabel, {14});
    for (const auto& instruction : instructions_) emitInstruction(instruction);
    if (!ended_) emit(words_, OpReturn, {});
    emit(words_, OpFunctionEnd, {});
    words_[3] = nextId_;
  }

  void emitVop3(const Instruction& instruction) {
    const auto loadSrc = [this](std::uint32_t source, std::uint32_t type) {
      if (source >= 256U) return load(3, sgpr(source - 256U));
      return load(type, vgpr(source));
    };
    const auto op = instruction.opcode;
    if (op == 0x14U) {
      const auto a = loadSrc(instruction.source0, 4);
      const auto b = loadSrc(instruction.source1, 4);
      const auto c = loadSrc(instruction.vsrc2, 4);
      const auto result = id();
      emit(words_, OpExtInst, {4, result, idGlsl_, 50, a, b, c});
      emit(words_, OpStore, {vgpr(instruction.destination), result});
    } else if (op == 0x15U) {
      const auto a = loadSrc(instruction.source0, 4);
      const auto b = loadSrc(instruction.source1, 4);
      const auto c = loadSrc(instruction.vsrc2, 4);
      const auto mul = id();
      emit(words_, OpFMul, {4, mul, a, b});
      const auto result = id();
      emit(words_, OpFAdd, {4, result, mul, c});
      emit(words_, OpStore, {vgpr(instruction.destination), result});
    } else if (op == 0x1DU || op == 0x1EU) {
      const auto a = loadSrc(instruction.source0, 4);
      const auto b = loadSrc(instruction.source1, 4);
      const auto c = loadSrc(instruction.vsrc2, 4);
      const auto ab = id();
      emit(words_, OpExtInst, {4, ab, idGlsl_, op == 0x1DU ? 37U : 40U, a, b});
      const auto result = id();
      emit(words_, OpExtInst, {4, result, idGlsl_, op == 0x1DU ? 37U : 40U, ab, c});
      emit(words_, OpStore, {vgpr(instruction.destination), result});
    }
  }

  void emitTextureSample(const Instruction& instruction) {
    if (!imageDeclared_) return;
    if (!vec2Declared_) {
      typeVec2_ = id();
      emit(words_, OpTypeVector, {typeVec2_, 4, 2});
      vec2Declared_ = true;
    }
    const auto u = load(4, vgpr(instruction.source0));
    const auto v = load(4, vgpr((instruction.source0 + 1U) % 256U));
    const auto uv = id();
    emit(words_, OpCompositeConstruct, {typeVec2_, uv, u, v});
    const auto sampledImage = id();
    emit(words_, OpLoad, {typeSampledImage_, sampledImage, sampledImageVar_});
    const auto texel = id();
    emit(words_, OpImageSampleImplicitLod, {5, texel, sampledImage, uv});
    for (std::uint32_t channel = 0; channel < 4U; ++channel) {
      const auto component = id();
      emit(words_, OpCompositeExtract, {4, component, texel, channel});
      emit(words_, OpStore, {vgpr((instruction.destination + channel) % 256U), component});
    }
  }

  void emitInstruction(const Instruction& instruction) {
    switch (instruction.format) {
    case Instruction::Format::Sop2: {
      const auto left = load(3, sgpr(instruction.source0));
      const auto right = load(3, sgpr(instruction.source1));
      const auto result = id();
      switch (instruction.opcode) {
      case 0x00U: emit(words_, OpIAdd, {3, result, left, right}); break;
      case 0x01U: emit(words_, OpISub, {3, result, left, right}); break;
      case 0x02U: emit(words_, OpBitwiseAnd, {3, result, left, right}); break;
      case 0x03U: emit(words_, OpBitwiseOr, {3, result, left, right}); break;
      case 0x04U: emit(words_, OpIMul, {3, result, left, right}); break;
      case 0x08U: emit(words_, OpBitwiseXor, {3, result, left, right}); break;
      case 0x0AU: emit(words_, OpShiftRightLogical, {3, result, left, right}); break;
      case 0x0EU: emit(words_, OpShiftLeftLogical, {3, result, left, right}); break;
      case 0x0FU: emit(words_, OpShiftRightLogical, {3, result, left, right}); break;
      case 0x10U: emit(words_, OpShiftRightArithmetic, {3, result, left, right}); break;
      default: return;
      }
      emit(words_, OpStore, {sgpr(instruction.destination), result});
      break;
    }
    case Instruction::Format::Vop2: {
      const auto left = load(4, vgpr(instruction.source0));
      const auto right = load(4, vgpr(instruction.source1));
      const auto result = id();
      switch (instruction.opcode) {
      case 0x00U: emit(words_, OpFAdd, {4, result, left, right}); emit(words_, OpStore, {vgpr(instruction.destination), result}); break;
      case 0x01U: emit(words_, OpFSub, {4, result, left, right}); emit(words_, OpStore, {vgpr(instruction.destination), result}); break;
      case 0x02U: emit(words_, OpFMul, {4, result, left, right}); emit(words_, OpStore, {vgpr(instruction.destination), result}); break;
      case 0x10U: {
        const auto previous = load(4, vgpr(instruction.destination));
        const auto mul = id();
        emit(words_, OpFMul, {4, mul, left, right});
        emit(words_, OpFAdd, {4, result, mul, previous});
        emit(words_, OpStore, {vgpr(instruction.destination), result});
        break;
      }
      case 0x11U: {
        const auto li = load(3, sgpr(instruction.source0));
        const auto ri = load(3, vgpr(instruction.source1));
        emit(words_, OpShiftLeftLogical, {3, result, ri, li});
        emit(words_, OpStore, {vgpr(instruction.destination), result});
        break;
      }
      case 0x14U:
      case 0x15U:
        emit(words_, OpExtInst, {4, result, idGlsl_, instruction.opcode == 0x14U ? 37U : 40U, left, right});
        emit(words_, OpStore, {vgpr(instruction.destination), result});
        break;
      case 0x1BU:
      case 0x1CU:
      case 0x1DU: {
        const auto li = load(3, sgpr(instruction.source0));
        const auto ri = load(3, sgpr(instruction.source1));
        const auto op = instruction.opcode == 0x1BU ? OpBitwiseAnd :
                        instruction.opcode == 0x1CU ? OpBitwiseOr : OpBitwiseXor;
        emit(words_, op, {3, result, li, ri});
        emit(words_, OpStore, {sgpr(instruction.destination), result});
        break;
      }
      default: break;
      }
      break;
    }
    case Instruction::Format::Vop3:
      emitVop3(instruction);
      break;
    case Instruction::Format::Vop1: {
      const auto source = load(4, vgpr(instruction.source0));
      const auto result = id();
      switch (instruction.opcode) {
      case 0x00U: emit(words_, OpCopyObject, {4, result, source}); break;
      case 0x01U: emit(words_, OpConvertSToF, {4, result, load(3, sgpr(instruction.source0))}); break;
      case 0x04U: emit(words_, OpFDiv, {4, result, 11, source}); break;
      case 0x05U: emit(words_, OpExtInst, {4, result, idGlsl_, 32, source}); break;
      case 0x20U: emit(words_, OpExtInst, {4, result, idGlsl_, 29, source}); break;
      case 0x21U: emit(words_, OpExtInst, {4, result, idGlsl_, 10, source}); break;
      case 0x22U: emit(words_, OpExtInst, {4, result, idGlsl_, 9, source}); break;
      case 0x24U: emit(words_, OpExtInst, {4, result, idGlsl_, 8, source}); break;
      case 0x27U: emit(words_, OpExtInst, {4, result, idGlsl_, 30, source}); break;
      case 0x33U: emit(words_, OpExtInst, {4, result, idGlsl_, 31, source}); break;
      case 0x35U: emit(words_, OpExtInst, {4, result, idGlsl_, 13, source}); break;
      case 0x36U: emit(words_, OpExtInst, {4, result, idGlsl_, 14, source}); break;
      case 0x0CU: emit(words_, OpConvertUToF, {4, result, load(3, vgpr(instruction.source0))}); break;
      case 0x0DU: emit(words_, OpConvertFToU, {3, result, source}); emit(words_, OpStore, {sgpr(instruction.destination), result}); return;
      case 0x0EU: emit(words_, OpConvertFToS, {3, result, source}); emit(words_, OpStore, {sgpr(instruction.destination), result}); return;
      default: return;
      }
      emit(words_, OpStore, {vgpr(instruction.destination), result});
      break;
    }
    case Instruction::Format::Vopc: {
      const auto left = load(4, vgpr(instruction.source0));
      const auto right = load(4, vgpr(instruction.source1));
      const auto result = id();
      emit(words_, OpFOrdGreaterThan, {2, result, left, right});
      break;
    }
    case Instruction::Format::Sopc:
      break;
    case Instruction::Format::Sopp:
      if (instruction.opcode == 0x01U) {
        emit(words_, OpReturn, {});
        ended_ = true;
      } else if (instruction.opcode == 0x02U &&
                 static_cast<std::int16_t>(instruction.word & 0xFFFFU) <= 0) {
        emit(words_, OpReturn, {});
        ended_ = true;
      }
      break;
    case Instruction::Format::Exp: {
      const auto target = instruction.expTarget;
      const bool isPosition = target == 12U;
      const bool isColour = target <= 7U;
      if (!isPosition && !isColour) break;
      const auto x = load(4, vgpr(instruction.source0));
      const auto y = load(4, vgpr(instruction.source1));
      const auto z = load(4, vgpr(instruction.vsrc2));
      const auto w = load(4, vgpr(instruction.vsrc3));
      const auto cx = (instruction.expEn & 1U) != 0U ? x : 10U;
      const auto cy = (instruction.expEn & 2U) != 0U ? y : 10U;
      const auto cz = (instruction.expEn & 4U) != 0U ? z : 10U;
      const auto cw = (instruction.expEn & 8U) != 0U ? w : 11U;
      const auto vector = id();
      emit(words_, OpCompositeConstruct, {5, vector, cx, cy, cz, cw});
      emit(words_, OpStore, {isPosition ? 15U : 12U, vector});
      break;
    }
    case Instruction::Format::Smem: {
      const auto uboIndex = instruction.source0 % 64U;
      for (std::uint32_t index = 0; index < 4U; ++index) {
        const auto iConst = id();
        emit(words_, OpConstant, {3, iConst, uboIndex + index});
        const auto chain = id();
        emit(words_, OpAccessChain, {ptrUboFloat_, chain, uboVar_, 0U, iConst});
        const auto value = id();
        emit(words_, OpLoad, {4, value, chain});
        emit(words_, OpStore, {sgpr(instruction.destination + index), value});
      }
      break;
    }
    case Instruction::Format::Vmem:
      if (((instruction.wideWord >> 54U) & 0x1U) != 0U) emitTextureSample(instruction);
      else {
        const auto source = load(4, vgpr(instruction.source0));
        emit(words_, OpStore, {vgpr(instruction.destination), source});
      }
      break;
    default:
      break;
    }
  }

  std::span<const std::uint32_t> bytecode_;
  ShaderType hint_{ShaderType::Auto};
  ShaderType type_{ShaderType::Auto};
  std::vector<Instruction> instructions_;
  std::vector<std::uint32_t> words_;
  std::unordered_map<std::uint32_t, std::uint32_t> sgprs_;
  std::unordered_map<std::uint32_t, std::uint32_t> vgprs_;
  std::uint32_t nextId_{20};
  bool hasMrt_{false};
  bool hasPosition_{false};
  bool ended_{false};
  bool imageDeclared_{false};
  bool vec2Declared_{false};
  bool sampleNeeded_{false};
  std::uint32_t typeImage_{0};
  std::uint32_t typeSampledImage_{0};
  std::uint32_t ptrSampledImage_{0};
  std::uint32_t sampledImageVar_{0};
  std::uint32_t typeVec2_{0};
  std::uint32_t idGlsl_{0};
  std::uint32_t typeUboArray_{0};
  std::uint32_t typeUbo_{0};
  std::uint32_t ptrUbo_{0};
  std::uint32_t uboVar_{0};
  std::uint32_t ptrUboFloat_{0};
  bool ptrUboFloat_declared_{false};
};

} // namespace

bool ShaderTranslator::translate(std::span<const std::uint32_t> gcnBytecode,
                                 ShaderType hint,
                                 std::vector<std::uint32_t>& spirvOut,
                                 std::string& error) {
  spirvOut.clear();
  Emitter emitter(gcnBytecode, hint);
  return emitter.run(spirvOut, error);
}

} // namespace aceps::shader
