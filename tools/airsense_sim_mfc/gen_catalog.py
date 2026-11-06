"""Generate VarCatalog.cpp from src/settings_defs.h.

    python gen_catalog.py

The MFC simulator embeds a snapshot of the firmware variable catalogue so the
exe works standalone, but the snapshot is regenerable from the single source.
"""

from __future__ import annotations

import pathlib
import re

HERE = pathlib.Path(__file__).resolve().parent
SOURCE = HERE.parent.parent / "src" / "settings_defs.h"
OUTPUT = HERE / "VarCatalog.cpp"

ROW = re.compile(
    r'\{\s*"([A-Z0-9]{3})"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*SET_(\w+)\s*,\s*'
    r'-?\d+\s*,\s*\d+\s*,\s*(NULL|"(?:[^"\\]|\\.)*"(?:\s*"(?:[^"\\]|\\.)*")*)\s*\}',
    re.S,
)
STRING = re.compile(r'"((?:[^"\\]|\\.)*)"', re.S)

# Chinese display names. Ventilation abbreviations and device-specific names
# without an established Chinese translation keep their original form.
CN = {
    "IPC": "设定压力", "MPA": "最大压力", "MPI": "最小压力",
    "IPP": "IPAP", "EPP": "EPAP", "MXI": "最大 IPAP", "MNE": "最小 EPAP",
    "SPT": "压力支持", "EEP": "EPAP", "EAI": "最小 EPAP", "EAX": "最大 EPAP",
    "EPI": "EPAP", "MNS": "最小 PS", "MXS": "最大 PS", "ANS": "最小 PS",
    "AXS": "最大 PS", "WPM": "最小 PS", "WPA": "最大 PS",
    "BKR": "后备呼吸频率", "RRT": "呼吸频率",
    "ITN": "最小 Ti", "ITX": "最大 Ti", "ITT": "Ti",
    "RST": "压力上升时间", "IBR": "目标呼吸频率",
    "EBE": "Easy-Breathe", "VTS": "触发灵敏度", "VCS": "切换灵敏度",
    "AFC": "Response", "WMV": "目标 Va", "PHT": "身高 (cm)",
    "STP": "Ramp 起始压力", "STU": "Ramp 起始压力",
    "EPS": "Ramp 起始 EPAP", "STV": "Ramp 起始 EPAP",
    "STE": "Ramp 起始 EPAP", "EAS": "Ramp 起始 EPAP", "IVS": "Ramp 起始 EPAP",
    "EPR": "EPR 等级", "EPA": "EPR", "EPX": "呼气压力释放（患者）",
    "EPT": "EPR 类型", "RMA": "Ramp", "RMT": "Ramp 时间",
    "ALR": "漏气报警", "SST": "SmartStart", "MSK": "面罩类型",
    "TBT": "管路类型", "QFC": "飞行模式", "MOP": "治疗模式",
    "CCO": "Climate Control", "HMS": "湿化等级", "HMX": "湿化",
    "HTF": "管路温度 (F)", "HTS": "管路温度 (C)", "HTX": "管路加热",
    "LAN": "语言", "PRD": "压力单位", "TMU": "温度单位",
    "ACC": "患者菜单 (Plus)", "ALV": "报警音量", "HLE": "高漏气报警",
    "NMF": "Non-Vent 面罩", "SPX": "低 SpO2 报警",
    "APX": "呼吸暂停报警 (s)", "LMA": "低 MV 报警",
}


def literal(text: str) -> str:
    text = text.replace("\\xc2\\xb0", "")
    text = text.replace('\\"', '"').replace("\\\\", "\\")
    return text.replace("\\", "\\\\").replace('"', '\\"').strip()


def cpp(text: str) -> str:
    return '"' + literal(text) + '"'


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8", errors="replace")
    rows = []
    for name, label, kind, raw_options in ROW.findall(source):
        options = "" if raw_options == "NULL" else "".join(STRING.findall(raw_options))
        cn = CN.get(name, literal(label))
        rows.append((name, literal(label), cn, kind == "ENUM", literal(options)))

    out = [
        "// GENERATED FILE - do not edit. Regenerate with: python gen_catalog.py",
        "// Source: src/settings_defs.h (VAR_CATALOG).",
        "",
        '#include "pch.h"',
        '#include "VarCatalog.h"',
        "",
        "namespace {",
        "struct RawVar {",
        "    const char* name; const char* label; const char* cn;",
        "    bool isEnum; const char* options;",
        "};",
        "const RawVar kRawVars[] = {",
    ]
    for name, label, cn, is_enum, options in rows:
        out.append('    { %s, %s, %s, %s, %s },' % (
            cpp(name), cpp(label), cpp(cn),
            "true" if is_enum else "false", cpp(options)))
    out += [
        "};",
        "} // namespace",
        "",
        "CVarCatalog::CVarCatalog()",
        "{",
        "    for (const RawVar& raw : kRawVars) {",
        "        VarDef def;",
        "        def.name = raw.name;",
        "        def.label = raw.label;",
        "        def.cn = raw.cn;",
        "        def.isEnum = raw.isEnum;",
        "        std::string options = raw.options ? raw.options : \"\";",
        "        size_t start = 0;",
        "        while (start < options.size()) {",
        "            size_t comma = options.find(',', start);",
        "            std::string item = (comma == std::string::npos)",
        "                ? options.substr(start) : options.substr(start, comma - start);",
        "            if (!item.empty()) def.options.push_back(item);",
        "            if (comma == std::string::npos) break;",
        "            start = comma + 1;",
        "        }",
        "        m_index[def.name] = m_vars.size();",
        "        m_vars.push_back(def);",
        "    }",
        "}",
        "",
        "const CVarCatalog& CVarCatalog::Instance()",
        "{",
        "    static CVarCatalog instance;",
        "    return instance;",
        "}",
        "",
        "const VarDef* CVarCatalog::Find(const std::string& name) const",
        "{",
        "    auto it = m_index.find(name);",
        "    return (it == m_index.end()) ? nullptr : &m_vars[it->second];",
        "}",
        "",
    ]
    OUTPUT.write_text("\n".join(out), encoding="utf-8")
    print("generated %s: %d variables" % (OUTPUT.name, len(rows)))


if __name__ == "__main__":
    main()
