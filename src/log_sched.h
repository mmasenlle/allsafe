
#include <boost/log/trivial.hpp>
extern size_t log_trace_level;
#define FLOG if (log_trace_level > 0) BOOST_LOG_TRIVIAL(info) << "FATAL "
#define ELOG if (log_trace_level > 1) BOOST_LOG_TRIVIAL(info) << "ERROR "
#define WLOG if (log_trace_level > 2) BOOST_LOG_TRIVIAL(info) << "WARN  "
#define ILOG if (log_trace_level > 3) BOOST_LOG_TRIVIAL(info) << "INFO  "
#define DLOG if (log_trace_level > 4) BOOST_LOG_TRIVIAL(info) << "DEBUG "
#ifndef NDEBUG
#define TLOG if (log_trace_level > 5) BOOST_LOG_TRIVIAL(info) << "TRACE "
#else
#define TLOG if (0) BOOST_LOG_TRIVIAL(info)
#endif
