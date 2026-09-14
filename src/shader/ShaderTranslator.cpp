/*
 * ShaderTranslator.cpp implements a conservative GCN-to-SPIR-V translator.
 * Unknown instructions are skipped, while known arithmetic and export forms
 * become real SPIR-V operations in a valid minimal shader module.
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

// SPIR-V opcodes used by the emitter.
enum Op : std::uint16_t {
  OpName = 5, OpMemoryModel = 14, OpEntryPoint = 15, OpExecutionMode = 16,
  OpCapability = 17, OpTypeVoid = 19, OpTypeBool = 20, OpTypeInt = 21,
  OpTypeFloat = 22, OpTypeVector = 23, OpTypeImage = 25, OpTypePointer = 32,
  OpTypeFunction = 33, OpConstant = 43, OpFunction = 54, OpFunctionEnd = 56,
  OpVariable = 59, OpLoad = 61, OpStore = 62, OpAccessChain = 65,
  OpDecorate = 71, OpCompositeConstruct = 80, OpCopyObject = 83,
  OpImageSampleImplicitLod = 87, OpConvertSToF = 115, OpIAdd = 128,
  OpFAdd = 129, OpISub = 130, OpFSub = 131, OpFMul = 133,
  OpFOrdGreaterThan = 170, OpBitwiseOr = 197, OpBitwiseAnd = 199,
  OpReturn = 253, OpLabel = 248,
};

constexpr std::uint32_t kCapabilityShader = 1;
constexpr std::uint32_t kAddressingLogical = 0;
constexpr std::uint32_t kMemoryModelGLSL450 = 1;
constexpr std::uint32_t kExecutionModelVertex = 0;
constexpr std::uint32_t kExecutionModelFragment = 4;
constexpr std::uint32_t kExecutionModelGLCompute = 5;
constexpr std::uint32_t kExecutionModeLocalSize = 17;
constexpr std::uint32_t kExecutionModeOriginUpperLeft = 7;
constexpr std::uint32_t kStorageFunction = 7;
constexpr std::uint32_t kStorageOutput = 3;
constexpr std::uint32_t kDecorationLocation = 30;

struct Instruction final {
  enum class Format : std::uint8_t { Unknown, Sop2, Sopc, Sopp, Vop1, Vop2, Vopc, Smem, Vmem, Exp };
  Format format{Format::Unknown};
  std::uint32_t word{0};
  std::uint64_t wideWord{0};
  std::uint32_t opcode{0};
  std::uint32_t destination{0};
  std::uint32_t source0{0};
  std::uint32_t source1{0};
};

Instruction decode(std::uint32_t word) noexcept {
  Instruction instruction;
  instruction.word = word;
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
  else if (((word >> 30U) & 0x3U) == 0x2U) instruction.format = Instruction::Format::Sop2;
  else if ((word & 0x80000000U) == 0) instruction.format = Instruction::Format::Vop2;
  instruction.opcode = (word >> 16U) & 0xFFU;
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
  words.push_back((static_cast<std::uint32_t>(operands.size()) + 1U) << 16U | static_cast<std::uint16_t>(op));
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
           instruction.format == Instruction::Format::Vmem) &&
          index + 1 < bytecode_.size()) {
        instruction.wideWord |= static_cast<std::uint64_t>(bytecode_[++index]) << 32U;
      }
      if (instruction.format == Instruction::Format::Exp) {
        const auto target = (instruction.word >> 23U) & 0x7U;
        hasMrt_ = hasMrt_ || target == 0;
        hasPosition_ = hasPosition_ || target == 7;
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
    emit(words_, OpVariable, {8, 12, kStorageOutput});
    emit(words_, OpDecorate, {12, kDecorationLocation, 0});
    const auto model = type_ == ShaderType::Vertex ? kExecutionModelVertex :
                       type_ == ShaderType::Fragment ? kExecutionModelFragment : kExecutionModelGLCompute;
    if (type_ == ShaderType::Fragment) emit(words_, OpExecutionMode, {13, kExecutionModeOriginUpperLeft});
    if (type_ == ShaderType::Compute) emit(words_, OpExecutionMode, {13, kExecutionModeLocalSize, 1, 1, 1});
    emitString(words_, OpEntryPoint, {model, 13, 12}, "main");
    nextId_ = 20;
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

  void emitInstruction(const Instruction& instruction) {
    switch (instruction.format) {
    case Instruction::Format::Sop2: {
      const auto left = load(3, sgpr(instruction.source0));
      const auto right = load(3, sgpr(instruction.source1));
      const auto result = id();
      const auto opcode = instruction.opcode;
      if (opcode == 0x00U) emit(words_, OpIAdd, {3, result, left, right});
      else if (opcode == 0x01U) emit(words_, OpISub, {3, result, left, right});
      else if (opcode == 0x02U) emit(words_, OpBitwiseAnd, {3, result, left, right});
      else if (opcode == 0x03U) emit(words_, OpBitwiseOr, {3, result, left, right});
      else return;
      emit(words_, OpStore, {sgpr(instruction.destination), result});
      break;
    }
    case Instruction::Format::Vop2: {
      const auto left = load(4, vgpr(instruction.source0));
      const auto right = load(4, vgpr(instruction.source1));
      const auto result = id();
      if (instruction.opcode == 0x00U) emit(words_, OpFAdd, {4, result, left, right});
      else if (instruction.opcode == 0x01U) emit(words_, OpFSub, {4, result, left, right});
      else if (instruction.opcode == 0x02U) emit(words_, OpFMul, {4, result, left, right});
      else return;
      emit(words_, OpStore, {vgpr(instruction.destination), result});
      break;
    }
    case Instruction::Format::Vop1: {
      const auto source = load(4, vgpr(instruction.source0));
      const auto result = id();
      if (instruction.opcode == 0x00U) emit(words_, OpCopyObject, {4, result, source});
      else if (instruction.opcode == 0x01U) emit(words_, OpConvertSToF, {4, result, load(3, sgpr(instruction.source0))});
      else return;
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
      // The low opcode byte is the GCN SOPP operation selector in this model.
      if ((instruction.word & 0xFFU) == 0x01U || instruction.opcode == 0x01U) {
        emit(words_, OpReturn, {});
        ended_ = true;
      }
      break;
    case Instruction::Format::Exp: {
      const auto value = load(4, vgpr(0));
      const auto vector = id();
      emit(words_, OpCompositeConstruct, {5, vector, value, value, value, 11});
      emit(words_, OpStore, {12, vector});
      break;
    }
    case Instruction::Format::Smem: {
      const auto source = load(3, sgpr(instruction.source0));
      emit(words_, OpStore, {sgpr(instruction.destination), source});
      break;
    }
    case Instruction::Format::Vmem: {
      const auto source = load(4, vgpr(instruction.source0));
      emit(words_, OpStore, {vgpr(instruction.destination), source});
      break;
    }
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
