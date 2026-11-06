#pragma once

#include "QFrame.h"
#include "VarCatalog.h"

namespace ci {

CString Widen(const std::string& text);         // UTF-8 / ASCII -> CString
std::string Narrow(const CString& text);        // CString -> UTF-8

CString FrameTypeName(char type);               // "Q" / "R" / "E" ...
CString FrameTypeLabel(char type);              // "Q 主机命令帧"
CString ErrorCodeText(const std::string& code); // four hex digits -> Chinese
CString CommandExplain(const std::string& command);
CString Brief(const qf::Frame& frame, bool outgoing);
CString Detail(const qf::Frame& frame, bool outgoing, const CString& stamp);

} // namespace ci
