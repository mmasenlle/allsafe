
#ifdef DEBUG
#define DEBUG_STR "-debug "
#else
#define DEBUG_STR " "
#endif // DEBUG
const char *main_version = "1.0.0beta" DEBUG_STR  __DATE__ " " __TIME__;
