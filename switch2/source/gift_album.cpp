// SPDX-License-Identifier: GPL-3.0-or-later
#include "gift_album.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sys/stat.h>
#include <cerrno>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace forge {
void GiftAlbum::load(const std::string& base, int generation, const Save& save)
{
    struct stat info{};
    if (::stat((base + ".bin").c_str(),&info) != 0 && errno == ENOENT) {
        archive.clear(); cards.clear(); groups.clear(); return;
    }
    auto raw = readGifts(base + ".bin", generation);
    auto extra = readGifts(base + ".extra",generation);
    raw.insert(raw.end(),std::make_move_iterator(extra.begin()),std::make_move_iterator(extra.end()));
    std::ifstream file(base + ".tsv");
    if (!file) throw std::runtime_error("Cannot open the Wonder Card index.");
    std::vector<AlbumCard> entries;
    const auto lang = save.data ? static_cast<int>(save.data->language()) : 0;
    for (std::string line; std::getline(file,line);) {
        std::istringstream input(line);
        std::array<std::string,22> f;
        for (auto& value : f) std::getline(input,value,'\t');
        const auto index = std::stoul(f[0]);
        auto& gift = raw.at(index);
        const int required = std::stoi(f[19]);
        bool fits = (!required || !lang || required == lang) && giftFitsSave(save,gift);
        entries.push_back({static_cast<unsigned>(index),static_cast<unsigned>(std::stoul(f[3])),
            static_cast<unsigned>(std::stoul(f[4])),static_cast<unsigned>(std::stoul(f[5])),
            static_cast<unsigned>(std::stoul(f[21])),f[6]=="1",fits,
            f[1],f[2],f[7],f[8],f[9],f[10],f[11],f[12],f[13],f[14],f[15],f[16],f[17],f[18],f[20]});
    }
    archive.swap(raw);
    cards.swap(entries);
    groups.clear();
}

static std::string fold(std::string_view text) {
    std::string out;
    for (size_t i=0;i<text.size();++i) {
        auto ch=static_cast<unsigned char>(text[i]);
        if (ch==0xc3 && i+1<text.size() && (text[i+1]=='\xa9' || text[i+1]=='\x89')) {out+='e'; ++i;}
        else out+=static_cast<char>(std::tolower(ch));
    }
    return out;
}

void GiftAlbum::filter(std::string_view query,std::string_view preferred)
{
    groups.clear();
    std::unordered_map<std::string,int> keys;
    std::istringstream words(fold(query));
    std::vector<std::string> terms;
    for (std::string term; words>>term;) terms.push_back(term);
    for (size_t i=0;i<cards.size();++i) {
        const auto& c=cards[i];
        if ((fitsOnly && !c.fits) || (shinyOnly && !c.shiny) || (kind==1 && !c.species) ||
            (kind==2 && c.species) || (!language.empty() && c.language!=language)) continue;
        if (status && staged.contains(c.key)!=(status==2)) continue;
        if (!terms.empty()) {
            auto text=fold(c.title+" "+c.subject+" "+c.language+" "+c.original+" "+c.ot+" "+c.tid+" "+c.series+" "+
                c.moves+" "+c.item+" "+c.ball+" "+c.met+" "+c.games+" "+c.contents+" "+std::to_string(c.number)+" "+
                archive[c.archive].name+" "+c.date);
            if (std::ranges::any_of(terms,[&](const auto& t){return text.find(t)==text.npos;})) continue;
        }
        if (collapse) {
            auto [it,added]=keys.emplace(c.key,groups.size());
            if (!added) {groups[it->second].copies.push_back(i);continue;}
        }
        groups.push_back({{static_cast<int>(i)},{}});
    }
    for (auto& g:groups) {
        auto rank=[&](int id){const auto& l=cards[id].language;return l==preferred?0:l=="ENG"?1:2;};
        std::stable_sort(g.copies.begin(),g.copies.end(),[&](int a,int b){return rank(a)<rank(b);});
        const auto& c=cards[g.copies.front()];
        switch(grouping) {
            case 0:g.section=c.date.empty()?"Undated":c.date.substr(0,4);break;
            case 1:g.section=c.series.empty()?"Other events":c.series;break;
            case 2:g.section=c.games.empty()?"Other games":c.games;break;
            case 3:g.section=c.subject.empty()?"Items":c.subject;break;
            default:g.section="All cards";
        }
    }
    std::stable_sort(groups.begin(),groups.end(),[&](const auto& x,const auto& y){
        if(x.section!=y.section) {
            if (grouping!=0) return x.section<y.section;
            if (x.section=="Undated" || y.section=="Undated") return y.section=="Undated";
            return sort==1?x.section<y.section:x.section>y.section;
        }
        const auto& a=cards[x.copies.front()]; const auto& b=cards[y.copies.front()];
        switch(sort) {
            case 0:return a.date==b.date?a.number>b.number:a.date>b.date;
            case 1:return a.date==b.date?a.number<b.number:a.date<b.date;
            case 2:return a.number<b.number;
            case 3:return a.species<b.species;
            case 4:return a.title<b.title;
            default:return a.level>b.level;
        }
    });
    for (size_t i=0;i<groups.size();) {
        size_t end=i+1;
        while(end<groups.size() && groups[end].section==groups[i].section) ++end;
        for(size_t j=i;j<end;++j) groups[j].sectionCount=static_cast<int>(end-i);
        i=end;
    }
}
}
