#include "content.h"
#include "theme.h"
#include "content_font_validation.h"
#include "spelling/spellcheck_core.h"
#include "spelling/suggestions.h"
#include "cJSON.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
namespace backup { void request_content(){} }
bool failed=false;
std::string read(const fs::path&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
bool fetch(const std::string &relative,const std::string &path,uint32_t limit,const std::string &hash){
    if(failed)return false;
    auto source=fs::path("build/content")/(relative=="catalog"?"catalog.json":relative);
    if(!fs::exists(source) || fs::file_size(source)>limit)return false;
    fs::copy_file(source,path,fs::copy_options::overwrite_existing);return true;
}
int main(int argc,char**){
    const std::string english="org.justtype.dictionary.en",theme="org.justtype.theme.ink",pack="org.justtype.pack.english";
    if(argc>1){content::init();content::tick();assert(!content::dictionary_available());assert(content::state().installed.empty());assert(!fs::exists("build/content-test-data/packages/org.justtype.dictionary.en/1.0.0/dictionary.bin"));assert(read("build/content-document-sentinel.txt")=="Document untouched");std::cout<<"Removal persisted across reboot; interrupted staging cleaned; damaged registry slot ignored; document preserved\n";return 0;}
    assert(fs::absolute("build/content-test-data").parent_path()==fs::current_path()/"build");
    fs::remove_all("build/content-test-data");
    fs::copy("build/content/seed/leaf_content","build/content-test-data",fs::copy_options::recursive);
    // Add Ink to this lifecycle fixture; fresh devices ship with only Light/Dark.
    const auto inkPath=fs::path("build/content-test-data/packages/org.justtype.theme.ink/1.0.0");
    fs::create_directories(inkPath);
    fs::copy_file("build/content/files/org.justtype.theme.ink/1.0.0/theme.json",inkPath/"theme.json",fs::copy_options::overwrite_existing);
    auto *seed=cJSON_Parse(read("build/content-test-data/seed.json").c_str());
    auto *catalogFixture=cJSON_Parse(read("build/content/catalog.json").c_str());
    auto *seedItems=cJSON_GetObjectItem(seed,"packages");auto *catalogItems=cJSON_GetObjectItem(catalogFixture,"packages");
    for(int i=0;i<cJSON_GetArraySize(catalogItems);++i){auto *v=cJSON_GetArrayItem(catalogItems,i);if(std::string(cJSON_GetObjectItem(v,"id")->valuestring)==theme)cJSON_AddItemToArray(seedItems,cJSON_Duplicate(v,true));}
    char *seedText=cJSON_PrintUnformatted(seed);std::ofstream("build/content-test-data/seed.json")<<seedText;cJSON_free(seedText);cJSON_Delete(seed);cJSON_Delete(catalogFixture);
    content::init();content::tick();assert(content::dictionary_available());assert(content::contains("hello"));assert(!content::contains("zzzzz"));
    int builtin=1;
    assert(content::theme_choices().size()==3);assert(content::select_theme(2,builtin));content::tick();assert(builtin==1);assert(content::active_theme_index(builtin)==2);assert(content::state().theme==theme);
    assert(content::select_theme(0,builtin));content::tick();assert(builtin==0);assert(content::state().theme.empty());assert(!content::select_theme(99,builtin));
    assert(spelling::normalize_utf8("ПРИВЕТ")=="привет");assert(spelling::normalize_utf8("ESPAÑOL")=="español");assert(spelling::normalize(std::string("\xc0\x80",2)).empty());
    spelling::Suggestions suggestions;suggestions.start("helo");for(int i=0;i<10000&&suggestions.pending();++i)suggestions.step(128);assert(!suggestions.pending());assert(suggestions.results()[0]=="hello" || suggestions.results()[1]=="hello");
    assert(content::disable(english));content::tick();assert(!content::dictionary_available());assert(!content::contains("hello"));assert(content::enable(english));content::tick();assert(content::contains("hello"));
    std::vector<content::Package> parsed;std::string error;
    assert(content::validate_catalog(read("build/content/catalog.json"),parsed,error));
    { Theme preview; const auto chosen=content::state().theme;
      assert(content::preview_theme("download:org.justtype.theme.midnight",preview));
      assert(content::preview_theme(theme,preview));
      assert(content::state().theme==chosen); }
    std::string invalid=read("build/content/catalog.json");auto at=invalid.find("dictionary.bin");invalid.replace(at,14,"../bad.bin");assert(!content::validate_catalog(invalid,parsed,error));
    assert(!content::validate_catalog(std::string(65537,'x'),parsed,error));
    // Publisher font fixture and corrupt/truncated variants are checked before LVGL.
    const auto font=read("build/content-test-font.bin");assert(content::valid_font(font,24));assert(!content::valid_font(font.substr(0,40),24));auto broken=font;broken[12]=char(255);assert(!content::valid_font(broken,24));
    auto remove=[&](const std::string&id){content::remove(id);assert(!content::needs_network());content::process(fetch);content::tick();};
    // Drop pack ownership, then independently installed items.
    remove(pack);remove(english);remove(theme);assert(content::state().installed.empty());assert(!content::dictionary_available());
    failed=true;content::refresh();assert(!content::process(fetch));assert(!content::state().busy);failed=false;
    content::refresh();assert(content::process(fetch));
    content::install(pack);assert(content::process(fetch));content::tick();assert(content::state().installed.size()==3);
    assert(content::enable(pack));content::tick();assert(content::dictionary_available());
    remove(english);assert(content::state().installed.size()==3); // pack still owns it
    remove(pack);assert(content::state().installed.empty());assert(!content::dictionary_available());
    failed=true;content::install(pack);assert(!content::process(fetch));assert(content::state().installed.empty());failed=false;
    content::install(pack);content::cancel();assert(!content::process(fetch));assert(content::state().installed.empty());
    // Explicit installs activate only the requested item; failures preserve it.
    const std::string newTheme="org.justtype.theme.midnight",newFont="org.justtype.font.basic";
    content::install(newTheme);assert(content::process(fetch));content::tick();assert(content::state().theme==newTheme);
    content::install(newFont);assert(content::process(fetch));content::tick();assert(content::state().font==newFont);
    failed=true;content::install("org.justtype.theme.rose");assert(!content::process(fetch));assert(content::state().theme==newTheme);failed=false;
    remove(newFont);remove(newTheme);
    // Spanish and Russian share glyph support. It survives the first uninstall,
    // then is reclaimed with the last dependent layout.
    const std::string spanish="org.justtype.layout.spanish",russian="org.justtype.layout.russian",glyphs="org.justtype.glyphs.international.v2";
    content::install(spanish);assert(content::process(fetch));
    assert(content::state().layout==spanish);
    auto has=[&](const std::string&id){auto s=content::state();return std::any_of(s.installed.begin(),s.installed.end(),[&](const auto&p){return p.id==id;});};
    for (const auto &test : std::vector<std::pair<std::string,std::string>>{{"org.justtype.dictionary.en.extended","hello"}}) {
        content::install(test.first);assert(content::process(fetch));assert(content::state().dictionary==test.first);content::tick();assert(content::contains(test.second));remove(test.first);
    }
    assert(has(spanish)&&has(glyphs));content::install(russian);assert(content::process(fetch));assert(has(russian)&&has(glyphs));assert(content::state().layout==russian);
    assert(content::enable(russian));content::tick();uint32_t glyph=0;bool isSpanish=false;
    assert(content::layout_key(13,false,glyph,isSpanish)&&glyph==0x439&&!isSpanish);
    remove(spanish);assert(!has(spanish)&&has(russian)&&has(glyphs));
    assert(content::layout_key(13,false,glyph,isSpanish)&&glyph==0x439);
    remove(russian);assert(!has(russian)&&!has(glyphs));assert(!content::layout_key(13,false,glyph,isSpanish));
    // A second dictionary replaces English, it cannot be active concurrently.
    std::string catalog=read("build/content/catalog.json");
    cJSON *j=cJSON_Parse(catalog.c_str());auto *items=cJSON_GetObjectItem(j,"packages");
    auto *ru=cJSON_Duplicate(cJSON_GetArrayItem(items,0),true);
    cJSON_ReplaceItemInObject(ru,"id",cJSON_CreateString("org.justtype.dictionary.ru"));
    cJSON_ReplaceItemInObject(ru,"language",cJSON_CreateString("ru"));
    auto *files=cJSON_GetObjectItem(ru,"files");auto *file=cJSON_GetArrayItem(files,0);
    const std::string words="LTXT\nпривет\nслово\n";
    cJSON_ReplaceItemInObject(file,"size",cJSON_CreateNumber(words.size()));cJSON_AddItemToArray(items,ru);
    char *out=cJSON_PrintUnformatted(j);std::ofstream("build/content/catalog.json")<<out;cJSON_free(out);cJSON_Delete(j);
    fs::create_directories("build/content/files/org.justtype.dictionary.ru/1.0.0");std::ofstream("build/content/files/org.justtype.dictionary.ru/1.0.0/dictionary.bin",std::ios::binary)<<words;
    content::refresh();assert(content::process(fetch));content::install(english);assert(content::process(fetch));assert(content::enable(english));content::tick();
    content::install("org.justtype.dictionary.ru");assert(content::process(fetch));assert(content::enable("org.justtype.dictionary.ru"));content::tick();
    assert(content::dictionary_language()=="ru");assert(content::contains("привет"));assert(!content::contains("hello"));
    remove("org.justtype.dictionary.ru");remove(english);assert(content::state().installed.empty());
    // Simulate power loss after assets were written, before a registry commit.
    std::ofstream("build/content-document-sentinel.txt")<<"Document untouched";
    for(const auto &entry:fs::recursive_directory_iterator("build/content/files"))if(entry.is_regular_file()){
        const auto target=fs::path("build/content-test-data/packages")/fs::relative(entry.path(),"build/content/files");
        fs::create_directories(target.parent_path());fs::copy_file(entry.path(),target,fs::copy_options::overwrite_existing);
    }
    std::ofstream("build/content-test-data/staging/transaction.json")<<read("build/content/catalog.json");
    int oldest=0;double lowest=1e20;
    for(int slot=0;slot<2;++slot){auto envelope=read("build/content-test-data/registry/"+std::to_string(slot)+".json");auto *e=cJSON_Parse(envelope.c_str());assert(e);auto *p=cJSON_Parse(cJSON_GetObjectItem(e,"payload")->valuestring);assert(p);double g=cJSON_GetObjectItem(p,"generation")->valuedouble;if(g<lowest){lowest=g;oldest=slot;}cJSON_Delete(e);cJSON_Delete(p);}
    std::ofstream("build/content-test-data/registry/"+std::to_string(oldest)+".json")<<"{\"payload\":\"interrupted";
    std::cout<<"Content lifecycle, offline removal, packs, rollback, cancellation, dictionaries, Unicode and font validation passed\n";
}
