#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <functional>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace MenuScript {
using Row = std::map<std::string, std::string>;
using Tables = std::map<std::string, std::vector<Row>>;
struct Line { int lineNo = 0; std::string text; std::vector<std::string> parts; };
inline std::string substitute(std::string value, const Row& fields) {
    for (const auto& field : fields) {
        const auto key = "${" + field.first + "}";
        size_t at = 0;
        while ((at = value.find(key, at)) != std::string::npos) {
            value.replace(at, key.size(), field.second); at += field.second.size();
        }
    }
    return value;
}
inline std::vector<Line> parse(const std::string& text) {
    std::vector<Line> result;
    std::istringstream input(text); std::string source; int number = 0;
    while (std::getline(input, source)) {
        ++number; std::string token; bool quoted = false, started = false;
        Line line{number, source, {}};
        for (size_t i = 0; i < source.size(); ++i) {
            const char ch = source[i];
            if (quoted && ch == '\\' && i + 1 < source.size() && source[i + 1] == '\"') {
                token.push_back('\"'); ++i; started = true;
            } else if (ch == '\"') { quoted = !quoted; started = true; }
            else if (!quoted && std::isspace(static_cast<unsigned char>(ch))) {
                if (started) { line.parts.push_back(token); token.clear(); started = false; }
            } else { token.push_back(ch); started = true; }
        }
        if (started) line.parts.push_back(token);
        if (!line.parts.empty() && !line.parts[0].empty() && line.parts[0][0] != '#') result.push_back(std::move(line));
    }
    return result;
}

// Static presentation for animated menus. The .menu owns labels, destinations,
// sprites and measurements; C++ retains the animation and game action handlers.
struct Control { std::string label, action, target, sprite; };
struct View {
    std::string title;
    Row values;
    std::map<std::string, Control> controls;
    std::vector<std::string> order;
    std::vector<std::string> ordered(std::initializer_list<const char*> ids) const {
        std::vector<std::string> result;
        for (const auto& id : order)
            for (const auto* allowed : ids) if (id == allowed) result.push_back(id);
        for (const auto* id : ids) if (std::find(result.begin(), result.end(), id) == result.end()) result.push_back(id);
        return result;
    }
    const std::string& text(const std::string& key) const {
        static const std::string empty;
        const auto found=values.find(key); return found==values.end()?empty:found->second;
    }
    float number(const std::string& key) const {
        try { const float value=std::stof(text(key)); return std::isfinite(value)?std::clamp(value,0.0f,4096.0f):0.0f; } catch (...) { return 0; }
    }
    const Control& control(const std::string& id) const {
        static const Control empty;
        const auto found=controls.find(id); return found==controls.end()?empty:found->second;
    }
};
inline View view(const std::string& source) {
    View result;
    for (const auto& line:parse(source)) {
        const auto& p=line.parts;
        if(p[0]=="title"&&p.size()>1)result.title=p[1];
        if(p[0]=="set"&&p.size()>2)result.values[p[1]]=p[2];
        if(p[0]=="control"&&p.size()>=4){
            if(!result.controls.count(p[1]))result.order.push_back(p[1]);
            result.controls[p[1]]={p[2],p[3],p.size()>4?p[4]:"",p.size()>5?p[5]:""};
        }
    }
    return result;
}

// Expand tokenized data, never reparse server/mod/user strings as script code.
inline std::vector<Line> expand(const std::vector<Line>& source, const Tables& tables,
        const std::function<bool(const std::string&)>& condition) {
    std::vector<Line> output;
    Row scriptValues;
    std::function<void(size_t,size_t,const Row&,int)> block;
    block = [&](size_t first, size_t last, const Row& fields, int depth) {
        if (depth > 32) throw std::runtime_error("MenuScript nesting limit exceeded");
        for (size_t i = first; i < last; ++i) {
            const auto& parts = source[i].parts;
            if (parts.empty()) continue;
            const auto& op = parts[0];
            if (op == "foreach" || op == "when") {
                if (parts.size() != 2) throw std::runtime_error("MenuScript block needs one argument");
                const auto end = op == "foreach" ? "endfor" : "endwhen";
                size_t close = i + 1, otherwise = last; int nesting = 1;
                for (; close < last; ++close) {
                    const auto& p = source[close].parts;
                    if (p.empty()) continue;
                    if (p[0] == op) ++nesting;
                    if (p[0] == end && --nesting == 0) break;
                    if (op == "when" && p[0] == "otherwise" && nesting == 1) otherwise = close;
                }
                if (close == last) throw std::runtime_error("Unclosed MenuScript block");
                if (op == "foreach") {
                    const auto found = tables.find(substitute(parts[1], fields));
                    if (found != tables.end()) for (const auto& row : found->second) {
                        Row scope = fields;
                        for (const auto& field : row) scope["row." + field.first] = field.second;
                        block(i + 1, close, scope, depth + 1);
                    }
                } else {
                    const auto local = fields.find(parts[1]);
                    const auto scripted = scriptValues.find(parts[1]);
                    auto truthy = [](const std::string& value) {
                        return !value.empty() && value != "0" && value != "false" && value != "off" && value != "no" && value != "null";
                    };
                    const bool yes = local != fields.end() ? truthy(local->second) :
                        scripted != scriptValues.end() ? truthy(scripted->second) : condition(parts[1]);
                    if (yes) block(i + 1, std::min(otherwise, close), fields, depth + 1);
                    else if (otherwise < close) block(otherwise + 1, close, fields, depth + 1);
                }
                i = close;
            } else {
                if (op == "endfor" || op == "endwhen" || op == "otherwise")
                    throw std::runtime_error("Unexpected MenuScript block terminator");
                auto line = source[i];
                for (auto& token : line.parts) token = substitute(token, fields);
                if (op == "set" && line.parts.size() >= 3) scriptValues[line.parts[1]] = substitute(line.parts[2], scriptValues);
                output.push_back(std::move(line));
                if (output.size() > 20000) throw std::runtime_error("MenuScript row limit exceeded");
            }
        }
    };
    block(0, source.size(), {}, 0);
    return output;
}
}
