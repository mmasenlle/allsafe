#include <string>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>
#include <boost/asio.hpp>
#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>
#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>
#include "fstream_utf8.h"
#include "mails.h"
#include "stats_dbs.h"
#include "web_utils.h"
#include "persistence.h"
#include "prios.h"
#include "props_sched.h"
#include "log_sched.h"
#include "cc_rules.h"
#include "dict.h"



#define MENU_DROPDOWN \
"<script>\n" \
"function myFunction() {\n" \
"    document.getElementById(\"myDropdown\").classList.toggle(\"show\");\n" \
"}\n" \
"window.onclick = function(event) {\n" \
"  if (!event.target.matches('.dropbtn')) {\n" \
"    var dropdowns = document.getElementsByClassName(\"dropdown-content\");\n" \
"    var i;\n" \
"    for (i = 0; i < dropdowns.length; i++) {\n" \
"      var openDropdown = dropdowns[i];\n" \
"      if (openDropdown.classList.contains('show')) {\n" \
"        openDropdown.classList.remove('show');\n" \
"      }\n" \
"    }\n" \
"  }\n" \
"}\n" \
"</script>\n"

#define MENU_DROPDOWN_DIV \
"<div class=\"dropdown\">\n" \
"<button onclick=\"myFunction()\" class=\"dropbtn\">MENU</button>\n" \
"  <div id=\"myDropdown\" class=\"dropdown-content\">\n" \
"    <a href=\"conf\">Configuraci&oacute;n</a>\n" \
"    <a href=\"explore\">Explorar</a>\n" \
"    <a href=\"pgraph\">Rendimiento</a>\n" \
"    <a href=\"menu\">M&aacute;s</a>\n" \
"  </div>\n" \
"</div>\n"

#define SCRIPT_WEBSOCKET \
"<script type=\"text/javascript\">\n" \
"   var loc = window.location;\n" \
"   var wsurl = \"ws://\" + loc.hostname + \":\" + loc.port + \"/websocket\";\n" \
"   var websocket = new WebSocket(wsurl, 'refresh');\n" \
"   websocket.onerror = function () {\n" \
"       document.getElementById(\"zone\").innerHTML = \"websocket connection error\";\n" \
"   };\n" \
"   websocket.onmessage = function (message) {\n" \
"   if (message.data[0] == '{') {\n" \
"       var msg = JSON.parse(message.data);\n" \
"       var c = document.getElementById(\"myCanvas\");" \
"       var ctx = c.getContext(\"2d\");\n" \
"       ctx.fillStyle = \"#FFFFFF\";\n" \
"       ctx.fillRect(0,0,482,50);\n" \
"       ctx.fillStyle = \"#666666\";\n" \
"       ctx.fillRect(75,50-msg.perc[0],56,msg.perc[0]);\n" \
"       ctx.fillRect(131,50-msg.perc[1],56,msg.perc[1]);\n" \
"       ctx.fillRect(187,50-msg.perc[2],56,msg.perc[2]);\n" \
"       ctx.fillRect(243,50-msg.perc[3],56,msg.perc[3]);\n" \
"       ctx.fillRect(299,50-msg.perc[4],56,msg.perc[4]);\n" \
"       ctx.fillRect(355,50-msg.perc[5],56,msg.perc[5]);\n" \
"       ctx.fillRect(411,50-msg.perc[6],56,msg.perc[6]);\n" \
"   } else {\n" \
"       document.getElementById(\"zone\").innerHTML = message.data;\n" \
"   }};\n" \
"</script>\n"

#define SCRIPT_CONFIRM \
"<script>\n" \
"function go_if(msg,url) {\n" \
"   if (confirm(msg) == true) window.location=url;\n" \
"}\n" \
"</script>\n"


extern std::string sdump(int op);
extern void sched_reinit();
//extern void sched_save_data();
extern void save_data();
extern void sched_nthreads();
extern void sched_nthreads_();
extern bool pausa;
extern bool resch_stop;
#ifdef COMP1
extern void sched_set_childs_prio(int prio);
#endif

extern std::string journal_to_string();
extern std::string sched_counters_str();
extern std::string dirs_dispatch(const std::vector<std::string> &vuri);
extern std::string dict_dispatch(const std::vector<std::string> &vuri);
extern std::string explore_dispatch(const std::vector<std::string> &vuri);
extern std::string explore_day_dispatch(const std::vector<std::string> &vuri);
#ifdef COMP1
extern std::string mssql_page(const std::vector<std::string> &vuri);
extern std::string oracle_page(const std::vector<std::string> &vuri);
extern std::string conf_dbs_dispatch(const std::vector<std::string> &vuri);
#endif
extern std::string ptasks_page(const std::vector<std::string> &vuri);
extern std::string conf_dispatch(const std::vector<std::string> &vuri);
extern std::string restore_folder_dispatch(const std::vector<std::string> &vuri);
extern void file_watcher_dump();
extern void clean_old_files(const std::string &);
extern void find_mod_files(const std::string &fpath, int i);
extern void clean_patchd();
extern void sched_counters_clear();
extern std::string websocket_accept_key(std::string key);
extern float *resources_perc();
extern float *resources_mean();
extern std::string sched_inprocess_str();
extern std::string perf_graph();
extern std::string sched_dump_counters();
extern std::string sched_graph_counters();
extern std::string file_watcher_nexts();
extern std::string sched_dump_lasts();
extern std::string sched_report();

extern void styles_load();
extern const std::string &get_styles();

static boost::asio::io_service io_service;

extern size_t def_prio;
size_t log_trace_level = 4;
int wserver_debug_state = 0;
static boost::thread *wth = NULL;
static boost::thread *uth = NULL;
static size_t http_port = 8680;
static size_t udp_port = 8680;
static size_t no_icons = 0;
#ifdef WIN32
static std::string http_addr = "127.0.0.1";
#else
static std::string http_addr = "0.0.0.0";
#endif // WIN32
static std::string favicon_file = "favicon.png";
static std::string user_pass;

void server(void);
void udp_server(void);

static boost::mutex mtx_wevent;
static boost::condition_variable cond_wevent;

static std::string wserver_graph_(float *data)
{
    std::string s = "<canvas id=\"myCanvas\" width=\"482\" height=\"50\" "
        "style=\"border:1px solid #ffffff;\" >NO CANVAS</canvas>\n"
    "<script>\n"
        "var c = document.getElementById(\"myCanvas\");\n"
        "var ctx = c.getContext(\"2d\");\n"
        "ctx.fillStyle = \"#555555\";\n";
    int x = 89, n; char buf[128];
    for (int i = 0; i < 7; i++, x += 56) {
        int y = (data[i]+.51)/2;
        n = sprintf(buf, "ctx.fillRect(%d,%d,56,%d);\n", x, 50-y, y);
        s.append(buf, n);
    }
    s += "</script>\n";
    return s;
}

std::string wserver_graph(float *data)
{
    std::string s = "<link rel=\"shortcut icon\" href=\"/favicon.ico\" />\n"
        "<link rel=\"stylesheet\" href=\"main.css\" type=\"text/css\" />\n"
//        "<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/4.7.0/css/font-awesome.min.css\" />\n"
        "</head><body>\n" MENU_DROPDOWN;
    s += wserver_graph_(data);
    s += "<br/><table class=\"gridtable\"><tr><td>" MENU_DROPDOWN_DIV "</td>\n";
    return s;
}

std::string wserver_ws_zone()
{
    char buf[64]; int n;
    float *perc = resources_perc();
    float *mean = resources_mean();
    std::string s = "<table class=\"gridtable\"><tr><td></td>\n"
        "<th>CPU</th><th>Mem</th><th>Swap</th><th>DiskR</th>"
        "<th>DiskW</th><th>NetR</th><th>NetW</th></tr>"
                   "<tr><th>Actual</th>";
    for (int i=0; i<7; i++) {
        n = sprintf(buf, "<td>%06.2f</td>", perc[i]);
        s.append(buf, n);
    }
    s += "</tr>\n<tr><th>Media 1h</th>";
    for (int i=0; i<7; i++) {
        n = sprintf(buf, "<td>%06.2f</td>", mean[i]);
        s.append(buf, n);
    }
    s += "</tr>\n</table>\n";
    s += sched_inprocess_str();
    return s;
}

static const std::string ws_head = "<!DOCTYPE html>\n"
        "<html><head><meta charset=\"utf-8\" />"
        "<link rel=\"shortcut icon\" href=\"/favicon.ico\" />\n"
        "<link rel=\"stylesheet\" href=\"main.css\" type=\"text/css\" />\n"
        "<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/4.7.0/css/font-awesome.min.css\" />\n"
        "<title>M U S C H E D</title></head>\n";
const std::string &wserver_pages_head() { return ws_head; }
static const std::string ws_tail = "</body></html>\n";
const std::string &wserver_pages_tail() { return ws_tail; }
static const std::string ws_script_confirm = SCRIPT_CONFIRM;
const std::string &wserver_confirm() { return ws_script_confirm; }
std::string wserver_ws_title(const std::string &h2_text)
{
    std::string s = ws_head;
    s += "<body><h2>"+h2_text+"</h2>\n<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><br/><br/>\n";
    return s;
}

std::string wserver_ws_page()
{
    std::string s = ws_head;
    s += "<body>\n" MENU_DROPDOWN SCRIPT_WEBSOCKET MENU_DROPDOWN_DIV
        "<br/><canvas id=\"myCanvas\" width=\"482\" height=\"50\" "
        "style=\"border:1px solid #ffffff;\" >NO CANVAS</canvas>\n"
        "<div id=\"zone\"></div>\n</body></html>";

    return s;
}

std::string wserver_menu_page()
{
    std::string s = ws_head;
    s += "<body><h2>Menu</h2><ul>\n"
        "<li><a href=\"clold\">clean oldd</a> limpieza del directorio old de ficheros que ya no existen</li>\n"
        "<li><a href=\"clpatchd\">clean patchd</a> limpieza del directorio patches de ficheros temporales</li>\n"
        "<li><a href=\"cdiff\">continuar</a> continuar procesamiento de ficheros</li>\n"
        "<li><a href=\"clcnt\">clear counters</a> resetea contadores de bytes y ficheros</li>\n"
        "<li><a href=\"conf\">configuration</a> p&aacute;gina de configuraci&oacute;n</li>\n"
        "<li><a href=\"counters\">counters</a> registro de contadores</li>\n"
#ifdef COMP1
        "<li><a href=\"dbs\">db config</a> p&aacute;gina de configuraci&oacute;n de bases de datos</li>\n"
        "<li><a href=\"db_stats\">db stats</a> p&aacute;gina de estad&iacute;sticas de bases de datos</li>\n"
#endif
        "<li><a href=\"debug\">debug</a> mostrar informaci&oacute;n de debug</li>\n"
        "<li><a href=\"dict\">dict</a> p&aacute;gina del diccionario</li>\n"
        "<li><a href=\"dirs\">dirs</a> p&aacute;gina de gesti&oacute;n de directorios</li>\n"
#ifdef COMP1
        "<li><a href=\"dstats\">dstats</a> estad&iacute;sticas de ficheros diferenciados</li>\n"
        "<li><a href=\"dupd\">dupdate</a> reajusta valores de diccionario</li>\n"
#endif
        "<li><a href=\"explore\">explore</a> p&aacute;gina de exploraci&oacute;n de ficheros procesados</li>\n"
        "<li><a href=\"expl_days\">explore days</a> p&aacute;gina de exploraci&oacute;n de ficheros procesados por d&iacute;as</li>\n"
        "<li><a href=\"nthreads\">hilos++</a> crear un hilo m&aacute;s de procesamiento</li>\n"
        "<li><a href=\"nthreads_\">hilos--</a> eliminar un hilo de procesamiento</li>\n"
        "<li><a href=\"/\">home</a> p&aacute;gina principal</li>\n"
        "<li><a href=\"/home\">home</a> p&aacute;gina principal no websocket</li>\n"
#ifdef COMP1
        "<li><a href=\"horarios\">horarios</a> registro de rendimientos por minuto</li>\n"
#endif
        "<li><a href=\"lasts\">lasts</a> &uacute;ltimos ficheros procesados</li>\n"
        "<li><a href=\"mailtest\">mailtest</a> env&iacute;a un email de test</li>\n"
        "<li><a href=\"menu\">menu</a> este men&uacute;</li>\n"
#ifdef COMP1
        "<li><a href=\"mssql\">mssql</a> p&aacute;gina de gesti&oacute;n de bases de datos MSSQL</li>\n"
#endif
        "<li><a href=\"nexts\">nexts</a> siguientes ficheros a procesar</li>\n"
        "<li><a href=\"stop\">parar</a> parar la ejecuci&oacute;n del servicio</li>\n"
#ifdef COMP1
        "<li><a href=\"oracle\">oracle</a> p&aacute;gina de gesti&oacute;n de bases de datos ORACLE</li>\n"
#endif
        "<li><a href=\"pdiff\">pausar</a> pausar procesamiento de ficheros</li>\n"
        "<li><a href=\"pending\">pending</a> ficheros en colas de pendientes</li>\n"
        "<li><a href=\"pgraph\">perf_graph</a> gr&aacute;fico de rendimiento</li>\n"
#ifdef COMP1
        "<li><a href=\"prediccion\">prediccion</a> predicci&oacute;n de uso de recursos</li>\n"
#endif
        "<li><a href=\"prio_\">prio low</a> establecer prioridad baja</li>\n"
        "<li><a href=\"prio\">prio normal</a> establecer prioridad normal</li>\n"
#ifdef COMP1
        "<li><a href=\"procesos\">procesos</a> registro de recursos por procesos</li>\n"
        "<li><a href=\"programas\">programas</a> registro de recursos por programa</li>\n"
#endif
        "<li><a href=\"ptasks\">ptasks</a> p&aacute;gina de gesti&oacute;n de comandos programados</li>\n"
        "<li><a href=\"report\">report</a> reporte diario de ficheros procesados</li>\n"
        "<li><a href=\"restdir\">restdir</a> restaurar directorio</li>\n"
        "<li><a href=\"rules\">rules</a> p&aacute;gina de gesti&oacute;n de reglas</li>\n"
        "<li><a href=\"save\">salvar</a> salvar datos</li>\n"
#ifdef COMP1
        "<li><a href=\"stats\">stats</a> estad&iacute;sticas de ficheros procesados</li>\n"
#endif
        "<li><a href=\"lvl\">tl++</a> aumentar la verbosidad de los logs</li>\n"
        "<li><a href=\"lvl_\">tl--</a> reducir la verbosidad de los logs</li>\n"
        "<li><a href=\"wait_event\">wait event</a> p&aacute;gina principal con espera de evento</li>\n"
#ifdef COMP1
        "<li><a href=\"fwdump\">watched files</a> generaci&oacute;n lista de ficheros modificados ("; s += dict_get("file_watcher.fwdump_fname"); s += ")</li>\n"
#endif
        "</ul></body></html>";
    return s;
}

static size_t kill_servers = 0;
static volatile bool new_event = false;
void wserver_notify_event()
{
//    boost::mutex::scoped_lock scoped_lock(mtx_wevent);
    new_event = true;
    cond_wevent.notify_all();
}

std::string wserver_serve(const std::string &uri)
{
    TLOG << "uri: " << uri;
    std::string s;
    std::vector<std::string> vuri;
    boost::char_separator<char> sep("?=&");
    boost::tokenizer<boost::char_separator<char> > tok(uri, sep);
    BOOST_FOREACH(const std::string &_s, tok) {
        vuri.push_back(_s);
    }
    if (!vuri.size()) vuri.push_back(uri);
    int op = 0;
    if (vuri.size() > 2 && vuri[2].size()) op = vuri[2][0] - '0';

    if (uri == "/") s = wserver_ws_page(); //most of the time
    else if (uri == "/status") s = "OK"; //most of the time
    else if (uri == "/home") s = sdump(0);
    else if (vuri[0] == "/wait_event") {
        { boost::mutex::scoped_lock scoped_lock(mtx_wevent);
        cond_wevent.timed_wait(scoped_lock, boost::posix_time::seconds(op > 0 ? op : 5)); }
        s = sdump(0);
    }
    else if (uri == "/programas") {
            s = sdump(1);
        } else if (uri == "/procesos") {
            s = sdump(2);
        } else if (uri == "/horarios") {
            s = sdump(3);
        } else if (uri == "/counters") {
            s = sched_dump_counters();
        } else if (uri == "/lasts") {
            s = wserver_ws_title("&Uacute;ltimos ficheros procesados") + sched_dump_lasts() + ws_tail;
        } else if (uri == "/nexts") {
            s = wserver_ws_title("Siguientes ficheros pendientes") + file_watcher_nexts() + ws_tail;
        } else if (uri == "/report") {
            s = wserver_ws_title("Reporte diario") + sched_report() + ws_tail;
        } else if (uri == "/prediccion") {
            s = sdump(4);
        } else if (uri == "/pending") {
            s = sdump(6);
        } else if (uri == "/stats") {
            s = sdump(10);
            s += persistence_stats();
        } else if (uri == "/save") {
            save_data();
//            sched_save_data();
            s = web_srefresh();
        } else if (uri == "/stop") {
            resch_stop = true;
//                  exit(0);
        } else if (uri == "/debug") {
            s = sdump(5);
        } else if (uri == "/pdiff") {
            pausa = true;
            s = web_srefresh();
        } else if (uri == "/cdiff") {
            styles_load(); // if you want to hot change styles
            sched_reinit();
            s = web_srefresh();
        } else if (uri == "/init") {
            sched_reinit();
            s = web_srefresh();
        } else if (uri == "/nthreads") {
            sched_nthreads();
            s = web_srefresh();
        } else if (uri == "/nthreads_") {
            sched_nthreads_();
            s = web_srefresh();
        } else if (uri == "/prio") {
#ifdef COMP1
            sched_set_childs_prio((def_prio=1));
#endif
            def_prio=1;
            s = web_srefresh();
        } else if (uri == "/prio_") {
#ifdef COMP1
            sched_set_childs_prio((def_prio=0));
#endif
            def_prio=0;
            s = web_srefresh();
        } else if (uri == "/lvl") {
            if (log_trace_level < 6) log_trace_level++;
            s = web_srefresh();
        } else if (uri == "/lvl_") {
            if (log_trace_level > 0) log_trace_level--;
            s = web_srefresh();
        } else if (vuri[0] == "/stats0") {
            s = sdump(10);
            s += persistence_dump0(op);
        } else if (vuri[0] == "/stats1") {
            s = sdump(10);
            s += persistence_dump1(1, op);
        } else if (vuri[0] == "/stats2") {
            s = sdump(10);
            s += persistence_dump1(2, op);
        } else if (vuri[0] == "/stats3") {
            s = sdump(10);
            s += persistence_dump1(3, op);
        } else if (vuri[0] == "/dstats") {
            s = sdump(10);
//            s += sched_files_str();
            s += diff_stats_dump(op);
        } else if (vuri[0] == "/explore") {
            s = explore_dispatch(vuri);
        } else if (vuri[0] == "/expl_days") {
            s = explore_day_dispatch(vuri);
        } else if (vuri[0] == "/dirs") {
            s = dirs_dispatch(vuri);
        } else if (vuri[0] == "/dict") {
            s = dict_dispatch(vuri);
        } else if (vuri[0] == "/dupd") {
            props::update();
            s = web_srefresh();
        } else if (vuri[0] == "/rules") {
            s = ccrules_page(vuri);
#ifdef COMP1
        } else if (vuri[0] == "/mssql") {
            s = mssql_page(vuri);
        } else if (vuri[0] == "/oracle") {
            s = oracle_page(vuri);
        } else if (vuri[0] == "/dbs") {
            s = conf_dbs_dispatch(vuri);
        } else if (vuri[0] == "/db_stats") {
            s = wserver_ws_title("Rendimiento bases de datos") + stats_dbs_dump(vuri) + ws_tail;
#endif
        } else if (vuri[0] == "/conf") {
            s = conf_dispatch(vuri);
        } else if (vuri[0] == "/restdir") {
            s = restore_folder_dispatch(vuri);
        } else if (vuri[0] == "/ptasks") {
            s = ptasks_page(vuri);
        } else if (vuri[0] == "/menu") {
            s = wserver_menu_page();
        } else if (vuri[0] == "/ws") {
            s = wserver_ws_page();
        } else if (vuri[0] == "/pgraph") {
            s = wserver_ws_title("Rendimiento") + perf_graph() + sched_graph_counters() + ws_tail;
        } else if (vuri[0] == "/fwdump") {
            file_watcher_dump();
            s = web_srefresh();
        } else if (vuri[0] == "/clpatchd") {
            clean_patchd();
            s = web_srefresh();
        } else if (vuri[0] == "/clcnt") {
            sched_counters_clear();
            s = web_srefresh();
        } else if (vuri[0] == "/mailtest") {
            mails_send_test();
            s = web_srefresh();
        } else if (vuri[0] == "/rldir") {
            if (vuri.size() > 2) {
                boost::thread th(boost::bind(find_mod_files, web_url_decode(vuri[2]), props::get().dirstowatch.size()+1));
                th.detach();
            }
            s = web_srefresh();
        } else if (vuri[0] == "/clold") {
            std::string dir;
            if (vuri.size() > 2) dir = web_url_decode(vuri[2]);
            boost::thread th(boost::bind(clean_old_files, dir));
            th.detach();
            s = web_srefresh();
//#ifdef WIN32
//        } else if (vuri[0] == "/journal") {
//            s = sdump(10);
//            s += journal_to_string();
//#endif
        } else if (uri == "/killw") {
            if (wth) {
                wth->interrupt();
                delete wth;
                wth = NULL;
            }
            s = web_srefresh();
        } else if (uri == "/killu") {
            if (uth) {
                uth->interrupt();
                delete uth;
                uth = NULL;
            }
            s = web_srefresh();
        } else if (uri == "/startw") {
            if (!wth) {
                wth = new boost::thread(server);
            } else {
                kill_servers = 1;
            }
            s = web_srefresh();
 /*       } else if (uri == "/startu") {
            if (!uth) {
                uth = new boost::thread(udp_server);
            }
            s = web_srefresh(); */
        } else {
            s = wserver_ws_page();
        }
    return s;
}

static void websocket_server(boost::shared_ptr<boost::asio::ip::tcp::socket> sock)
{
    try {
        new_event = true;
        for (;;) {
            std::string s2, s = "{\n\"id\": 17,\n\"perc\":[";
            float *perc = resources_perc();
            for (int i = 0; i < 7; i++) {
                int y = (perc[i]+.51)/2;
                s += boost::lexical_cast<std::string>(y);
                if (i < 6) s += ",";
            }
            s += "]\n}";
            s2.clear(); s2.push_back(0x81); s2.push_back(s.size());
//TLOG << "websocket_server()->sending1("<<s.size()<<"): '" << s << "'";
            boost::asio::write(*sock, boost::asio::buffer(s2 + s));
            s = wserver_ws_zone();
            s2.clear(); s2.push_back(0x81); if (s.size() < 126) s2.push_back(s.size());
            else { s2.push_back(126); s2.push_back(s.size()/256); s2.push_back(s.size()%256); }
//TLOG << "websocket_server()->sending2("<<s.size()<<"): '" << s << "'";
            boost::asio::write(*sock, boost::asio::buffer(s2 + s));
            boost::this_thread::sleep(boost::posix_time::milliseconds(70)); // take air
            if (!new_event) {
                boost::mutex::scoped_lock scoped_lock(mtx_wevent);
                cond_wevent.timed_wait(scoped_lock, boost::posix_time::seconds(3));
            }
            new_event = false;
        }
    } catch(std::exception& e) {
        DLOG << "websocket_server() error: " << e.what(); // a lot of broken pipes, is normal
    }
}

#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/algorithm/string.hpp>

static std::string decode64(const std::string &val) {
    return std::string(boost::archive::iterators::transform_width<boost::archive::iterators::binary_from_base64<std::string::const_iterator>, 8, 6>(val.begin()), boost::archive::iterators::transform_width<boost::archive::iterators::binary_from_base64<std::string::const_iterator>, 8, 6>(val.end()));
}

static void wserver_serv0(boost::shared_ptr<boost::asio::ip::tcp::socket> sock)
{
        try {
    char data[1500];
    boost::system::error_code error;
    size_t length = sock->read_some(boost::asio::buffer(data), error);
    if (error == boost::asio::error::eof) {
        return;    // Connection closed cleanly by peer.
    } else if (error) {
//                std::cerr << "Error in read some" << error << std::endl;
        throw boost::system::system_error(error); // Some other error.
    }
TLOG << "wserver()->read(" << length << "): '" << std::string(data, length) << "'";
    if (!user_pass.empty()) {
        bool auth_ok = false;
        std::string req = std::string(data, length);
static const std::string authorization = "Authorization: Basic ";
        size_t pos0 = req.find(authorization);
        if (pos0 != std::string::npos) {
//TLOG << "wserver()->read: 'Authorization: Basic' found";
            size_t posn = req.find("\r\n", pos0);
            pos0 = pos0 + authorization.size();
            if (posn != std::string::npos && posn > pos0) { try {
                while (posn > pos0 && req[posn-1] == '=') --posn;
                std::string enc_auth = req.substr(pos0, posn-pos0);
        TLOG << "wserver()->enc_auth: '"<<enc_auth<<"'";
                std::string req_auth = decode64(enc_auth);
                TLOG << "wserver()->req_auth: '" << req_auth << "'";
                if (req_auth == user_pass) auth_ok = true;
            } catch (std::exception &e) { WLOG << "wserver()->decode64 error: " << e.what(); }}
        }
        if (!auth_ok) {
            std::string s = "HTTP/1.0 401 Unauthorized status\r\n"
                "WWW-Authenticate: Basic realm=\"Login\"\r\n\r\n";
            boost::asio::write(*sock, boost::asio::buffer(s));
            return;
        }
    }
    std::string uri;
    for (int i = 4; i < length && data[i] != ' '; i++) {
        uri.push_back(data[i]);
    }
    if (uri == "/favicon.ico") {
        bool not_found = true;
        try {
            size_t n1 = boost::filesystem::file_size(favicon_file), n2 = 0;
            ifstream_utf8 ifs(favicon_file.c_str(), ifstream_utf8::binary);
            if (ifs) {
                std::stringstream ss;
                ss << "HTTP/1.0 200 OK\r\nContent-Length: " << n1 << "\r\n";
                ss << "Content-Type: image/x-icon\r\n\r\n";
                boost::asio::write(*sock, boost::asio::buffer(ss.str())); char buf[4096];
                for (;;) {
                    ifs.read(buf, sizeof(buf)); if (ifs.gcount() <= 0) break;
                    boost::asio::write(*sock, boost::asio::buffer(buf, ifs.gcount()));
                    n2 += ifs.gcount();
                }
            }
            TLOG << "wserver(" << uri << ")->size: " << n1 << "/" << n2;
            not_found = false;
        } catch(std::exception &e) {
            WLOG << "wserver(" << uri << ")->Error: " << e.what();
        }
        if (not_found) {
            std::string s = "HTTP/1.0 404 Not Found\r\n\r\n";
            boost::asio::write(*sock, boost::asio::buffer(s));
        }
    } else if (uri == "/main.css") {
        std::stringstream ss;
        ss << "HTTP/1.0 200 OK\r\nContent-Length: " << get_styles().size() << "\r\n";
        ss << "Content-Type: text/css\r\n\r\n";
        boost::asio::write(*sock, boost::asio::buffer(ss.str()));
        boost::asio::write(*sock, boost::asio::buffer(get_styles()));
    } else if (uri == "/websocket") {
        static const char secwebkey[] = "Sec-WebSocket-Key: ";
        std::string key;
        for (int i = 0; i < length - 30; i++) {
            if (strncmp(data + i, secwebkey, sizeof(secwebkey) - 1) == 0) {
                for (int j = i + sizeof(secwebkey) - 1; j < length; j++) {
                    if (!isprint(data[j]) || isspace(data[j])) break;
                    key.push_back(data[j]);
                }
                break;
            }
        }
TLOG << "wserver()->websocket.key: '" << key << "'";
        std::string s = "HTTP/1.1 101 Switching Protocols\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        "Sec-WebSocket-Protocol: refresh\r\n"
                        "Sec-WebSocket-Accept: ";
std::string skey = websocket_accept_key(key);
TLOG << "wserver()->websocket.skey("<<skey.size()<<"): '" << skey << "'";
                            s += skey + "\r\n\r\n";
        boost::asio::write(*sock, boost::asio::buffer(s));
//        boost::thread th(boost::bind(websocket_server, sock));
//        th.detach();
        websocket_server(sock);

    } else if (uri.compare(0, 9, "/restore/") == 0) {
        std::string fp = props::get().tmpd + "/" + web_url_decode(uri.substr(9));
        try {
            std::stringstream ss; size_t n1 = boost::filesystem::file_size(fp);
            ss << "HTTP/1.0 200 OK\r\nContent-Length: " << n1 << "\r\n";
            ss << "Content-Type: application/octet-stream\r\n\r\n";
            boost::asio::write(*sock, boost::asio::buffer(ss.str()));
            char buf[4096]; size_t n2=0;
            ifstream_utf8 ifs(fp.c_str(), ifstream_utf8::binary);
            for (;;) {
                ifs.read(buf, sizeof(buf)); if (ifs.gcount() <= 0) break;
                boost::asio::write(*sock, boost::asio::buffer(buf, ifs.gcount()));
                n2 += ifs.gcount();
            }
            TLOG << "wserver(" << uri << ")->size: " << n1 << "/" << n2;
        } catch(std::exception &e) {
            WLOG << "wserver(" << uri << "): " << e.what();
            std::string s = "HTTP/1.0 500 Internal server error\r\n\r\n";
            boost::asio::write(*sock, boost::asio::buffer(s));
        }
        try { boost::filesystem::remove(fp); } catch(std::exception &e) { WLOG << "wserver(" << uri << ")->remove: " << e.what(); }
    } else {
        std::string s = wserver_serve(uri);
        std::stringstream ss;
        ss << "HTTP/1.0 200 OK\r\nContent-Length: " << s.size() << "\r\n";
        ss << "Content-Type: text/html\r\n\r\n";
        boost::asio::write(*sock, boost::asio::buffer(ss.str()));
        boost::asio::write(*sock, boost::asio::buffer(s));
    }

    } catch(std::exception& e) {
        WLOG << "wserver_serv0() error: " << e.what(); // a lot of broken pipes, is normal
    }
}

void server(void)
{
    TLOG << "server()";
//    prios_set_thread_prio(5);

    for (;;) {
    try {
    boost::asio::ip::tcp::acceptor a(io_service, boost::asio::ip::tcp::endpoint(
        boost::asio::ip::address::from_string(http_addr), http_port));
#ifdef WIN32
        {  SOCKET _s = a.native_handle();
           if (!SetHandleInformation((HANDLE)_s, HANDLE_FLAG_INHERIT, 0)) {
                fprintf(stderr, "wserver::SetHandleInformation->ERROR %d\n", GetLastError());
           }}
#endif // WIN32
    for (;;) {
//        boost::system::error_code error;
//        boost::asio::ip::tcp::socket sock(io_service);
        boost::shared_ptr<boost::asio::ip::tcp::socket> sock(new boost::asio::ip::tcp::socket(io_service));
        a.accept(*sock);
        boost::thread th(boost::bind(wserver_serv0, sock));
        th.detach();

        if (kill_servers) {
            kill_servers = 0;
            WLOG << "WebServer()->About to restart ... ";
            break;
        }
    }
    } catch(boost::system::system_error& e) {
        WLOG << "Web server error: " << e.what();
    }
  }
}
/*
void udp_server()
{
    TLOG << "udp_server()";
  try
  {
    boost::asio::ip::udp::socket socket(io_service,
        boost::asio::ip::udp::endpoint(
            boost::asio::ip::address::from_string("127.0.0.1"), udp_port));

        for (;;)
        {
          char data[1500];
          boost::asio::ip::udp::endpoint remote_endpoint;
          boost::system::error_code error;
          size_t length = socket.receive_from(boost::asio::buffer(data),
              remote_endpoint, 0, error);

          if (error && error != boost::asio::error::message_size)
            throw boost::system::system_error(error);

                std::string uri;
                if (data[0] != 'G') uri.assign(data, length);
                else for (int i = 4; i < length && data[i] != ' '; i++) {
                    uri.push_back(data[i]);
                }
          std::string message = wserver_serve(uri);

          boost::system::error_code ignored_error;
          socket.send_to(boost::asio::buffer(message),
              remote_endpoint, 0, ignored_error);
        }
    } catch(boost::system::system_error& e) {
        WLOG << "UDP server error: " << e.what();
    }
}
*/
void wserver_init()
{
    TLOG << "wserver_init()";

    dict_set("wserver.log_trace_level", &log_trace_level);
    dict_set("wserver.http_port", &http_port);
    dict_set("wserver.udp_port", &udp_port);
    dict_set("wserver.no_icons", &no_icons);
    dict_set("wserver.kill_servers", &kill_servers);
    dict_set("wserver.http_addr", &http_addr);
    dict_set("wserver.favicon_file", &favicon_file);
    dict_set_enc("wserver.user_pass", &user_pass);

    styles_load();

    wth = new boost::thread(server);
//    uth = new boost::thread(udp_server);
}

std::string web_icon(const std::string &icon, std::string text)
{
    return (!no_icons) ? icon : text;
}


std::string web_srefresh(std::string p)
{
    std::string s = "<!DOCTYPE html>\n<html><head>"
        "<meta charset=\"utf-8\"/><meta http-equiv=\"refresh\" content=\"0;url=/" + p + "\"/></head></html>";
    return s;
}
static int hex_to_val(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
std::string web_url_decode(const std::string &url)
{
    std::string s;
    for (int i = 0, n = url.size(); i < n; ++i) {
        if (url[i] == '%' && ((i+2) < url.size())) {
            s.push_back(hex_to_val(url[i+1])*16 + hex_to_val(url[i+2]));
            i += 2;
        } else {
            if (url[i] == '+') s.push_back(' ');
            else s.push_back(url[i]);
        }
    }
    return s;
}
static const char hexchars[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
std::string web_encode(const std::string &str)
{
    std::string s;
    for (int i = 0, n = str.size(); i < n; ++i) {
        if ((str[i] < 0) || !isalnum(str[i])) {
            s.push_back('%');
            s.push_back(hexchars[((unsigned char)str[i])/16]);
            s.push_back(hexchars[((unsigned char)str[i])%16]);
        } else {
            s.push_back(str[i]);
        }
    }
    return s;
}
/*
#ifndef NDEBUG
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE (main_test_suite_ws)

BOOST_AUTO_TEST_CASE (ws_tests)
{
    std::string enc = "%24+%26+%3C+%3E+%3F+%3B+%23+%3A+%3D+%2C+%22+%27+%7E+%2B+%25";
    BOOST_CHECK( web_url_decode(enc) == "$ & < > ? ; # : = , \" ' ~ + %" );
    std::string dec = "\\/\\h ó l ä -ñoño %@|€*+{}";
    BOOST_CHECK( web_url_decode(web_encode(dec)) == dec );

    wserver_serve("/lvl_");
    size_t log_trace_lvl = log_trace_level;
    wserver_serve("/lvl_");
    wserver_serve("/lvl");
    wserver_serve("/lvl");
    wserver_serve("/lvl_");
    wserver_serve("/lvl");
    BOOST_CHECK( log_trace_lvl == (log_trace_level - 1) );

    BOOST_CHECK( wserver_serve("/menu?pepe=3") == wserver_serve("/menu?fu=man&chu=0&stop=1") );
}

BOOST_AUTO_TEST_SUITE_END( )

#endif
*/
