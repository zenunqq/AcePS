// AcePS runtime smoke test: exercises a minimal mapped-memory and guest-bootstrap lifecycle.
#include "runtime/emulation_session.h"
#include <iostream>
int main(){AcePS::Runtime::EmulationSession session;std::string reason;if(!session.loadBootstrap(reason)){std::cerr<<reason<<'\n';return 1;}return session.runOneFrame()?1:0;}
