
#ifdef NDEBUG
#define SERVERVERSION "harvid " ICSVERSION " ["ICSARCH"]"
#else
#define SERVERVERSION "harvid " ICSVERSION " ["ICSARCH" debug]"
#endif

#define HTMLBODY \
	"<body style=\"width:900px; margin:0 auto;\">"\
	"<div style=\"text-align:center;\"><img alt=\"Harvid\" src=\"/logo.jpg\"/></div>\n"

#define HTMLFOOTER \
	"<hr/><div style=\"text-align:center; color:#888;\">"SERVERVERSION" at %s:%i</div>"
