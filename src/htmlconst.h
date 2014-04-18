
#ifdef NDEBUG
#define SERVERVERSION "harvid " ICSVERSION " ["ICSARCH"]"
#else
#define SERVERVERSION "harvid " ICSVERSION " ["ICSARCH" debug]"
#endif

#define HTMLBODYPERC(P) \
  "<body style=\"width:900px; margin:110px auto 0 auto;\">" \
  "<div style=\"position:fixed; height:100px; width:100%"P"; top:0; left:0; background:#000; text-align:center;\"><a href=\"/\"><img alt=\"Harvid\" src=\"/logo.jpg\" style=\"border:0px;\"/></a></div>\n"

#define HTMLBODY HTMLBODYPERC("%")

#define CENTERDIV \
  "<div style=\"width:38em; margin:0 auto;\">\n"

#define HTMLFOOTER \
  "<hr/><div style=\"text-align:center; color:#888;\">"SERVERVERSION" at %s:%i</div>"

#define ERRFOOTER \
  "<hr/><div style=\"text-align:center; color:#888;\">"SERVERVERSION"</div>\n</body></html>"

#define OK200MSG(TXT) \
  DOCTYPE HTMLOPEN "<title>harvid admin</title></head>" HTMLBODYPERC("") "<p>OK. " TXT " command successful</p>" ERRFOOTER
