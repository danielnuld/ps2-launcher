#pragma once
// Interface language (phase 18): config.ini [ui] idioma = es | en, read first by load_config. Each on-screen text is
// written L("español", "English") where it is used; Spanish until the config is read (the first splash frames).
extern int lang_en;
#define L(es, en) (lang_en ? (en) : (es))
