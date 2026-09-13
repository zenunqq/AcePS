// AcePS input-router implementation: returns stable neutral controller states until platform input adapters are added.
#include "engine/controls/input_router.h"
#include <stdexcept>
namespace AcePS::Controls { const ControllerState& InputRouter::controller(unsigned i)const{if(i>=4)throw std::out_of_range("Controller index is outside AcePS's supported range.");return controllers_[i];} }
