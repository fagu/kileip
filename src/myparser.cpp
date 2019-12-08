/********************************************************************************
*   Copyright (C) Fabian Gundlach 2010 (320pointsguy@googlemail.com)            *
*********************************************************************************/

/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

// TODO strange paragraph handling: "merry%\n\nchristmas" makes a new paragraph, but "merry%\nchristmas" doesn't make a space

#include <QDebug>
#include <QFile>
#include <QStandardPaths>
#include <QStringList>
#include "myparser.h"
#include <QMutex>
#include <QMutexLocker>

Range::Range(int start, int end) : m_start(start), m_end(end) {
    assert(start <= end+1);
}

QString Range::source(const QString& text) const {
    return text.mid(m_start, m_end-m_start+1);
}

bool Range::equal(const QString &text, const QString &ref) const {
    if (m_end-m_start+1 != ref.length())
        return false;
    for (int a = 0; a < m_end-m_start+1; a++)
        if (text.at(m_start+a) != ref.at(a))
            return false;
    return true;
}

PPart to_ptr(const UPart& p) {
    return std::visit([](auto&& arg) -> PPart {return arg.get();}, p);
}

PCommandPart to_ptr(const UCommandPart& p) {
    return std::visit([](auto&& arg) -> PCommandPart {return arg.get();}, p);
}

std::vector<PPart> to_ptr(const std::vector<UPart>& v) {
    std::vector<PPart> res;
    for (const UPart& p : v)
        res.push_back(to_ptr(p));
    return res;
}

std::optional<PPart> to_ptr(const std::optional<UPart>& v){
    if (v.has_value())
        return to_ptr(v.value());
    else
        return std::nullopt;
}

UPart generalize(UCommandPart&& p) {
    return std::visit([](auto&& arg) -> UPart {return std::move(arg);}, p);
}

PPart generalize(const PCommandPart& p) {
    return std::visit([](auto&& arg) -> PPart {return arg;}, p);
}

Range range(PPart part) {
    return std::visit([](auto&& arg) {return arg->range();}, part);
}

Range command_name_range(const PCommandPart& part) {
    return std::visit(overloaded{
        [](const PPrimitiveCommandPart& bc) {
            return bc->range();
        },
        [](const PArgsCommandPart& bc) {
            return bc->name_range();
        },
    }, part);
}

CommentPart::CommentPart(const Range& range) : m_range(range) {
}

PrimitiveCommandPart::PrimitiveCommandPart(const Range& range) : m_range(range) {
}

ArgsCommandPart::ArgsCommandPart(const Range& range, const Range& name_range, std::vector<UPart > && args, std::optional<UPart> && optional) : m_range(range), m_name_range(name_range), m_args(std::move(args)), m_optional(std::move(optional)) {
}

TextPart::TextPart(const Range& range, std::vector<UPart > && parts) : m_range(range), m_parts(std::move(parts)) {
}

QString TextPart::sourceWithoutVoid(const QString &text) const {
    if (m_parts.size() == 1) {
        if (std::holds_alternative<UEnvironmentPart >(m_parts[0])) {
            const UEnvironmentPart& ep = std::get<UEnvironmentPart >(m_parts[0]);
            if (command_name_range(ep->begin()).source(text) == "{")
                return ep->body()->range().source(text);
        }
    }
    return m_range.source(text);
}

EnvironmentPart::EnvironmentPart(const Range& range, UCommandPart && begin, std::optional<UCommandPart> && end, UTextPart && body) : m_range(range), m_begin(std::move(begin)), m_end(std::move(end)), m_body(std::move(body)) {
}


Ending::Ending(QString w) : wanted(w) {
}

Parser::Parser(const QString& text, int start) : m_text(text), i(start) {
    Global::init();
}

UTextPart Parser::parse() {
    m_textdata = m_text.constData();
    endings.emplace_back(QString(""));
    return parseText();
}

UTextPart Parser::parseText() {
    int start = i;
    int end;
    std::vector<UPart > parts;
    bool lastnewline = false;
    while(true) {
        if (i >= m_text.length()) {
            end = i-1;
            break;
        } else {
            if (m_textdata[i] == '\n') {
                if (lastnewline) {
                    while(i > 0 && m_textdata[i-1].isSpace())
                        i--;
                    while(m_textdata[i] != '\n')
                        i++;
                    int cp_start = i;
                    while(i < m_text.length()-1 && m_textdata[i+1].isSpace())
                        i++;
                    while(m_textdata[i] != '\n')
                        i--;
                    int cp_end = i;
                    UPrimitiveCommandPart cp = std::make_unique<PrimitiveCommandPart>(Range(cp_start, cp_end));
                    auto cp2 = endingFound(std::move(cp));
                    if (!cp2) {
                        i++;
                        end = cp_start-1;
                        break;
                    } else {
                        parts.emplace_back(generalize(std::move(cp2.value())));
                    }
                } else {
                    lastnewline = true;
                }
            } else if (!m_textdata[i].isSpace()) {
                lastnewline = false;
            }
            if (m_textdata[i] == '%') {
                UCommentPart cp = parseComment();
                parts.emplace_back(std::move(cp));
            } else if (m_textdata[i] == '\\') {
                UArgsCommandPart cp = parseCommand();
                int cp_start = cp->range().m_start;
                QString commandname = cp->name_range().source(m_text);
                bool anyargs = !cp->args().empty();
                auto cp2 = endingFound(std::move(cp));
                if (!cp2) {
                    end = cp_start-1;
                    break;
                } else if ((commandname == "\\begin" && anyargs) || Global::specialenvs.contains(commandname)) {
                    UEnvironmentPart ep = parseEnvironment(std::move(cp2.value()));
                    parts.emplace_back(std::move(ep));
                } else {
                    parts.emplace_back(generalize(std::move(cp2.value())));
                }
            } else if (m_textdata[i] == '{') {
                UPrimitiveCommandPart cp = parsePrimitiveCommand();
                UEnvironmentPart ep = parseEnvironment(std::move(cp));
                parts.emplace_back(std::move(ep));
            } else if (m_textdata[i] == '}' || m_textdata[i] == ']') {
                UPrimitiveCommandPart cp = parsePrimitiveCommand();
                int cp_start = cp->range().m_start;
                auto cp2 = endingFound(std::move(cp));
                if (!cp2) {
                    end = cp_start-1;
                    break;
                } else {
                }
            } else if (m_textdata[i] == '$') {
                UPrimitiveCommandPart cp = parsePrimitiveCommand();
                int cp_start = cp->range().m_start;
                auto cp2 = expectedEndingFound(std::move(cp));
                if (!cp2) {
                    end = cp_start-1;
                    break;
                } else {
                    UEnvironmentPart ep = parseEnvironment(std::move(cp2.value()));
                    parts.emplace_back(std::move(ep));
                }
            } else {
                i++;
            }
        }
        if (endings.size() && endings.back().wanted == "1") {
            end = i-1;
            break;
        }
    }
    return std::make_unique<TextPart>(Range(start, end), std::move(parts));
}

UCommentPart Parser::parseComment() {
    int start = i;
    int end;
    while(true) {
        if (i >= m_text.length()) {
            end = i-1;
            break;
        } else if (m_textdata[i] == '\n') {
            end = i;
            i++;
            break;
        }
        i++;
    }
    return std::make_unique<CommentPart>(Range(start, end));
}

int numArgs(const Range &name_range, std::optional<PPart> arg0, const QString& text) {
    QString name = name_range.source(text);
    if (Global::commands.contains(name))
        return Global::commands[name];
    else if (name == "\\begin") {
        if (arg0.has_value()) {
            QString envname = "E" + std::get<PTextPart>(arg0.value())->sourceWithoutVoid(text);
            if (Global::commands.contains(envname))
                return Global::commands[envname]+1;
            else
                return -1;
        } else
            return 1;
    }
    else
        return -1;
}

UArgsCommandPart Parser::parseCommand() {
    int start = i;
    int end;
    int nameend = -1;
    std::vector<UPart> args;
    std::optional<UPart> optional;
    i++;
    bool lastnewline = false;
    while(true) {
        if (i >= m_text.length()) {
            if (nameend == -1)
                nameend = i-1;
            while(i > 0 && m_textdata[i-1].isSpace())
                i--;
            end = i-1;
            break;
        } else {
            if (nameend == -1) {
                if (code(m_textdata[i]) != 11) {
                    if (i != start+1) {
                        nameend = i-1;
                    } else {
                        nameend = i;
                        i++;
                    }
                } else {
                    i++;
                }
            } else {
                if (!m_textdata[i].isSpace()) {
                    lastnewline = false;
                    if (m_textdata[i] == '%') {
                        UCommentPart comment = parseComment();
                        // TODO Handle comments
                    } else if (m_textdata[i] == '}') {
                        end = i-1;
                        break;
                    } else if (m_textdata[i] == '[') {
                        UPrimitiveCommandPart ccp = parsePrimitiveCommand();
                        UEnvironmentPart arg = parseEnvironment(std::move(ccp));
                        optional = std::move(arg);
                    } else {
                        int na = numArgs(Range(start, nameend), args.empty() ? std::optional<PPart>() : std::make_optional(to_ptr(args[0])), m_text);
                        if (na > (int)args.size() || (na == -1 && m_textdata[i] == '{')) {
                            endings.emplace_back(QString("1"));
                            UTextPart arg = parseText();
                            endings.pop_back();
                            args.emplace_back(std::move(arg));
                        } else {
                            while(i > 0 && m_textdata[i-1].isSpace())
                                i--;
                            end = i-1;
                            break;
                        }
                    }
                } else {
                    if (m_textdata[i] == '\n') {
                        if (lastnewline) {
                            i--;
                            while(i > 0 && m_textdata[i-1].isSpace())
                                i--;
                            end = i-1;
                            break;
                        } else {
                            lastnewline = true;
                        }
                    }
                    i++;
                }
            }
        }
    }
    return std::make_unique<ArgsCommandPart>(Range(start, end), Range(start, nameend), std::move(args), std::move(optional));
}

UPrimitiveCommandPart Parser::parsePrimitiveCommand() {
    int start = i;
    int end = i;
    i++;
    return std::make_unique<PrimitiveCommandPart>(Range(start, end));
}

UEnvironmentPart Parser::parseEnvironment(UCommandPart&& begincommand) {
    int start = std::visit([](auto&& T) {return T->range().m_start;}, begincommand);
    int end;
    Range commandnamerange = command_name_range(to_ptr(begincommand));
    QString commandname = commandnamerange.source(m_text);
    QString wanted;
    bool epoe = true;
    if (commandname == "{")
        wanted = "}";
    else if (commandname == "[")
        wanted = "]";
    else if (commandname == "\\begin")
        wanted = "E" + range(std::get<UArgsCommandPart >(begincommand)->args().at(0)).source(m_text);
    else if (Global::specialenvs.contains(commandname)) {
        wanted = Global::specialenvs[commandname].endcommand;
        epoe = Global::specialenvs[commandname].partofenvironment;
    }
    endings.emplace_back(wanted);
    UTextPart body = parseText();
    std::optional<UCommandPart> endcommand;
    Ending& ne = endings.back();
    if (wanted == "$" && ne.cp.has_value() && m_text[range(generalize(to_ptr(ne.cp.value()))).m_start] == '\n')
        epoe = false;
    if (epoe) {
        endcommand = std::move(ne.cp);
        if (endcommand.has_value())
            end = range(generalize(to_ptr(endcommand.value()))).m_end;
        else {
            end = body->range().m_end;
            i = end+1;
        }
    } else {
        end = body->range().m_end;
        i = end+1;
    }
    endings.pop_back();
//     assert(endcommand.has_value());
    return std::make_unique<EnvironmentPart>(Range(start, end), std::move(begincommand), std::move(endcommand), std::move(body));
}

std::optional<UCommandPart> Parser::endingFound(UCommandPart&& cp) {
    if (endings.empty())
        return std::move(cp);
    if (matches(endings.back().wanted, to_ptr(cp))) {
        endings.back().cp = std::move(cp);
        return std::nullopt;
    }
    for (int k = (int)endings.size()-2; k >= 0; k--) {
        if (matches(endings[k].wanted, to_ptr(cp))) {
            i = range(generalize(to_ptr(cp))).m_start;
            return std::nullopt;
        }
    }
    return std::move(cp);
}

std::optional<UCommandPart> Parser::expectedEndingFound(UCommandPart&& cp) {
    if (endings.empty())
        return std::move(cp);
    if (matches(endings.back().wanted, to_ptr(cp))) {
        endings.back().cp = std::move(cp);
        return std::nullopt;
    }
    return std::move(cp);
}

bool Parser::matches(QString wanted, PCommandPart cp) {
    if (wanted.length() >= 1 && wanted.at(0) == 'E') {
        return std::visit(overloaded{
            [this,&wanted](PArgsCommandPart cp) -> bool {
                return cp->name_range().equal(m_text, QString("\\end")) && cp->args().size() && "E" + range(cp->args()[0]).source(m_text) == wanted;
            },
            [](PPrimitiveCommandPart) -> bool {
                return false;
            },
        }, cp);
    } else {
        return command_name_range(cp).source(m_text) == wanted || (wanted == "$" && m_text[range(generalize(cp)).m_start] == '\n');
    }
}


int Parser::code(QChar c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
        return 11;
    return -1; // TODO Return letter-codes as in the TeXbook
}


QMutex global_init_mutex;
QMap<QString,int> Global::commands;
QMap<QString,SpecialEnvironment> Global::specialenvs;

void Global::init() {
    QMutexLocker lock(&global_init_mutex);
    if (!commands.empty())
        return;
    QString commandsfilename = QStandardPaths::locate(QStandardPaths::DataLocation, "parser/commands.txt");
    qDebug() << "commands.txt at " << commandsfilename;
    QFile comfile(commandsfilename);
    if (!comfile.exists())
        qDebug() << "commands.txt missing";
    comfile.open(QIODevice::ReadOnly | QIODevice::Text);
    
    QTextStream in(&comfile);
    in.setCodec("UTF-8");
    int state = 0;
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.at(0) == '#')
            continue;
        if (state == 0) { // Read number of arguments for commands
            if (line == "!envs")
                state = 1;
            else {
                int space = line.indexOf(' ');
                int numargs = line.right(line.length()-space-1).replace(" ","").toInt();
                commands[line.left(space)] = numargs;
            }
        } else if (state == 1) { // Read special environments
            QStringList toks = line.split(QRegExp("\\s+"));
            SpecialEnvironment se;
            se.endcommand = toks[1];
            se.partofenvironment = (toks[2].toInt() != 0);
            specialenvs[toks[0]] = se;
        }
    }
    comfile.close();
}
