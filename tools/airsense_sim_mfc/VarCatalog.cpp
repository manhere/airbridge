// GENERATED FILE - do not edit. Regenerate with: python gen_catalog.py
// Source: src/settings_defs.h (VAR_CATALOG).

#include "pch.h"
#include "VarCatalog.h"

namespace {
struct RawVar {
    const char* name; const char* label; const char* cn;
    bool isEnum; const char* options;
};
const RawVar kRawVars[] = {
    { "IPC", "Set Pressure", "设定压力", false, "" },
    { "MPA", "Max Pressure", "最大压力", false, "" },
    { "MPI", "Min Pressure", "最小压力", false, "" },
    { "IPP", "IPAP", "吸气相正压（IPAP）", false, "" },
    { "EPP", "EPAP", "呼气相正压（EPAP）", false, "" },
    { "MXI", "Max IPAP", "最大吸气相正压（IPAP）", false, "" },
    { "MNE", "Min EPAP", "最小呼气相正压（EPAP）", false, "" },
    { "SPT", "Pressure Support", "PS（压力支持）", false, "" },
    { "EEP", "EPAP", "呼气相正压（EPAP）", false, "" },
    { "EAI", "Min EPAP", "最小呼气相正压（EPAP）", false, "" },
    { "EAX", "Max EPAP", "最大呼气相正压（EPAP）", false, "" },
    { "EPI", "EPAP", "呼气相正压（EPAP）", false, "" },
    { "MNS", "Min PS", "最小 PS（压力支持）", false, "" },
    { "MXS", "Max PS", "最大 PS（压力支持）", false, "" },
    { "ANS", "Min PS", "最小 PS（压力支持）", false, "" },
    { "AXS", "Max PS", "最大 PS（压力支持）", false, "" },
    { "WPM", "Min PS", "最小 PS（压力支持）", false, "" },
    { "WPA", "Max PS", "最大 PS（压力支持）", false, "" },
    { "BKR", "Backup Rate", "后备呼吸频率（BPM）", false, "" },
    { "RRT", "Resp. Rate", "呼吸频率", false, "" },
    { "ITN", "Ti Min", "最小吸气时间（Ti）", false, "" },
    { "ITX", "Ti Max", "最大吸气时间（Ti）", false, "" },
    { "ITT", "Ti", "吸气时间（Ti）", false, "" },
    { "RST", "Rise Time (ms)", "压力上升时间（Rise Time）", false, "" },
    { "IBR", "Target Pt Rate", "目标呼吸频率（BPM）", false, "" },
    { "EBE", "Easy-Breathe", "Easy-Breathe", true, "Off,On" },
    { "VTS", "Trigger Sens.", "触发灵敏度", true, "Very Low,Low,Med,High,Very High" },
    { "VCS", "Cycle Sens.", "切换灵敏度", true, "Very Low,Low,Med,High,Very High" },
    { "AFC", "Response", "Response", true, "Standard,Soft" },
    { "WMV", "Target Va", "目标肺泡通气量（Va）", false, "" },
    { "PHT", "Height (cm)", "身高（cm）", false, "" },
    { "STP", "Ramp Start Pressure", "Ramp（延迟升压）起始压力", false, "" },
    { "STU", "Ramp Start Pressure", "Ramp（延迟升压）起始压力", false, "" },
    { "EPS", "Ramp Start EPAP", "Ramp（延迟升压）起始 EPAP", false, "" },
    { "STV", "Ramp Start EPAP", "Ramp（延迟升压）起始 EPAP", false, "" },
    { "STE", "Ramp Start EPAP", "Ramp（延迟升压）起始 EPAP", false, "" },
    { "EAS", "Ramp Start EPAP", "Ramp（延迟升压）起始 EPAP", false, "" },
    { "IVS", "Ramp Start EPAP", "Ramp（延迟升压）起始 EPAP", false, "" },
    { "EPR", "EPR Level", "EPR（呼气释压）等级", false, "" },
    { "EPA", "EPR (clinical)", "EPR（呼气释压）[临床]", true, "Off,On" },
    { "EPX", "Pressure Relief (patient)", "EPR（呼气释压）[患者]", true, "Off,On" },
    { "EPT", "EPR Type", "EPR（呼气释压）类型", true, "Ramp Only,Full Time" },
    { "RMA", "Ramp", "Ramp（延迟升压）", true, "Off,On,Auto" },
    { "RMT", "Ramp Time (min)", "Ramp（延迟升压）时间（min）", false, "" },
    { "ALR", "Leak Alert", "漏气报警", true, "Off,On" },
    { "SST", "SmartStart", "智能启动", true, "Off,On" },
    { "MSK", "Mask Type", "面罩类型", true, "Pillows,Full Face,Nasal,Pediatric" },
    { "TBT", "Tube Type", "管路类型", true, "SlimLine,Standard,3m" },
    { "QFC", "Airplane Mode", "飞行模式", true, "Off,On" },
    { "MOP", "Therapy Mode", "治疗模式", true, "CPAP,AutoSet,APAP,S,ST,T,VAuto,ASV,ASVAuto,iVAPS,PAC,AutoSet Her" },
    { "CCO", "Climate Control", "温湿度控制", true, "Auto,Manual" },
    { "HMS", "Humidity Level", "湿化等级", false, "" },
    { "HMX", "Humidity", "湿化", true, "Off,On" },
    { "HTF", "Tube Temp (°F)", "管路温度（°F）", false, "" },
    { "HTS", "Tube Temp (°C)", "管路温度（°C）", false, "" },
    { "HTX", "Tube Heating", "管路加热", true, "Off,On,Auto" },
    { "LAN", "Language", "语言", true, "English,French,German,Italian,Spanish(EU),Spanish(US),Portuguese(EU),Portuguese(US),Dutch,Swedish,Danish,Norwegian,Finnish,Japanese,Russian,Turkish,Chinese(Trad),Chinese(Simp),Polish,Japanese(KN),Czech" },
    { "PRD", "Pressure Units", "压力单位", true, "cmH2O,hPa" },
    { "TMU", "Temp Units", "温度单位", true, "°C,°F" },
    { "ACC", "Patient Access", "患者菜单（Plus）", true, "Plus,On" },
    { "ALV", "Alarm Volume", "报警音量", true, "Low,Med,High" },
    { "HLE", "High Leak Alarm", "高漏气报警", true, "Off,On" },
    { "NMF", "Non-Vent Mask", "Non-Vent 面罩", true, "Off,On" },
    { "SPX", "Low SpO2 Alarm", "低 SpO2 报警", false, "" },
    { "APX", "Apnea Alarm (s)", "呼吸暂停报警（s）", false, "" },
    { "LMA", "Low MV Alarm", "低 MV 报警", false, "" },
};
} // namespace

CVarCatalog::CVarCatalog()
{
    for (const RawVar& raw : kRawVars) {
        VarDef def;
        def.name = raw.name;
        def.label = raw.label;
        def.cn = raw.cn;
        def.isEnum = raw.isEnum;
        std::string options = raw.options ? raw.options : "";
        size_t start = 0;
        while (start < options.size()) {
            size_t comma = options.find(',', start);
            std::string item = (comma == std::string::npos)
                ? options.substr(start) : options.substr(start, comma - start);
            if (!item.empty()) def.options.push_back(item);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        m_index[def.name] = m_vars.size();
        m_vars.push_back(def);
    }
}

const CVarCatalog& CVarCatalog::Instance()
{
    static CVarCatalog instance;
    return instance;
}

const VarDef* CVarCatalog::Find(const std::string& name) const
{
    auto it = m_index.find(name);
    return (it == m_index.end()) ? nullptr : &m_vars[it->second];
}
