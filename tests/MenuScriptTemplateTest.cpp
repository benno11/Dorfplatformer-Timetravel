#include "MenuScriptTemplate.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
using namespace MenuScript;
static std::string read(const std::filesystem::path& path) {
    std::ifstream in(path); assert(in); return {std::istreambuf_iterator<char>(in), {}};
}
int main() {
    const auto windowsPath = parse(R"(control file "File" open "" "C:\mods\file.png")");
    assert(windowsPath[0].parts.size() == 6 && windowsPath[0].parts[4].empty());
    assert(windowsPath[0].parts[5] == R"(C:\mods\file.png)");
    Tables tables{{"entries", {{{"name", "first"}, {"available", "true"}},
                                {{"name", "second"}, {"available", "false"}}}}};
    const auto source = parse("when enabled\nforeach entries\nwhen row.available\nbutton \"${row.name}\" menu \"destination\"\notherwise\ntext \"${row.name}\"\nendwhen\nendfor\notherwise\ntext \"disabled\"\nendwhen\n");
    const auto enabled = expand(source, tables, [](const std::string&) { return true; });
    assert(enabled.size()==2 && enabled[0].parts[0]=="button" && enabled[0].parts[1]=="first");
    assert(enabled[1].parts[0]=="text" && enabled[1].parts[1]=="second");
    const auto disabled = expand(source, tables, [](const std::string&) { return false; });
    assert(disabled.size()==1 && disabled[0].parts[1]=="disabled");
    assert(expand(parse("foreach missing\ntext \"unreachable\"\nendfor"), tables, {}).empty());
    // Untrusted data must stay inside one token, even if it resembles script.
    const std::string hostile = "quote\"\nbutton \"Quit\" quit\n${row.name}";
    tables["untrusted"]={{{"name",hostile}}};
    auto safe=expand(parse("foreach untrusted\ntext \"${row.name}\" literal_log\nendfor"),tables,{});
    assert(safe.size()==1 && safe[0].parts.size()==3 && safe[0].parts[1]==hostile);
    const auto localFlag = expand(parse("set visible false\nwhen visible\ntext hidden\notherwise\ntext shown\nendwhen"), {}, [](const std::string&) { return true; });
    assert(localFlag.size() == 2 && localFlag.back().parts[1] == "shown");
    bool rejected=false;
    try { expand(parse("when enabled\ntext \"broken\""),tables,[](const std::string&){return true;}); }
    catch(const std::runtime_error&) {rejected=true;}
    assert(rejected);
    // The uploaded achievement menu selects titles for earned rows and
    // requirements for locked rows, using boolean data rather than state labels.
    tables["achievements"] = {
        {{"title", "Earned title"}, {"requirements", "Completed requirement"}, {"unlocked", "true"}},
        {{"title", "Locked title"}, {"requirements", "Missing requirement (0/1)"}, {"unlocked", "false"}}
    };
    const auto achievementMenu = expand(parse(read("assets/menus/achievements.menu")), tables,
        [](const std::string&) { return false; });
    std::vector<Line> achievementButtons;
    for (const auto& line : achievementMenu)
        if (line.parts.size() >= 4 && line.parts[0] == "button" && line.parts[2] == "copy")
            achievementButtons.push_back(line);
    assert(achievementButtons.size() == 2);
    assert(achievementButtons[0].parts[1] == "Earned title" && achievementButtons[0].parts[3] == "Earned title");
    assert(achievementButtons[1].parts[1] == "Missing requirement (0/1)" && achievementButtons[1].parts[3] == "Missing requirement (0/1)");
    // Every shipped menu parses and expands under both feature states.
    size_t count=0;
    for(const auto& file:std::filesystem::directory_iterator("assets/menus")) {
        if(file.path().extension()!=".menu")continue;
        const auto program=parse(read(file.path())); assert(!program.empty());
        for(bool value:{false,true}) expand(program,tables,[&](const std::string&){return value;});
        ++count;
    }
    assert(count>=43);
    const auto pause=view(read("assets/menus/pause.menu"));
    assert(pause.title=="PAUSED" && pause.control("resume").action=="resume");
    assert(pause.control("settings").target=="mainsetting" && pause.control("settings").sprite=="settingbtn");
    assert(pause.number("button_scale")==0.75f);
    const auto title=view(read("assets/menus/title.menu"));
    assert(title.control("play").target=="mainplay");
    const auto quit=view(read("assets/menus/quitconfirm.menu"));
    assert(quit.control("close").action=="quit" && quit.number("width")==360);
    const auto editor=view(read("assets/menus/editor.menu"));
    assert(editor.control("save_exit").action=="save_exit");
    const auto order=editor.ordered({"resume","save_resume","exit","save_exit"});
    assert(order[1]=="save_resume" && order[2]=="exit");
    assert(editor.control("exit").action=="save_play" && editor.control("exit").label=="SAVE & PLAY");
    assert(view(read("assets/menus/editorresize.menu")).number("panel_width")==520);
    std::cout<<count<<" menu templates validated; nesting, conditions, safe row expansion and animated controls passed\n";
}
