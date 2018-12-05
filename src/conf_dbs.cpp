#include <boost/lexical_cast.hpp>
#include "props_sched.h"
#include "web_utils.h"
#include "cc_rules.h"

static const std::string shead = "<body><table class=\"new_db\"><tr><td><h2 class=\"wizard\">Wizard de configuraci&oacute;n de base de datos</h2>\n"
    "</td></tr><tr><td><center><table class=\"wizard\"><tr><td>\n";
static const std::string stail = "<br/></td></tr></table></center></td></tr></table><br/>\n"
    "<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a></body></html>";

enum { DB_TYPE_MSSQL, DB_TYPE_ORACLE, DB_TYPE_MYSQL, DB_TYPE_OTHER, DB_TYPE_LAST };
static const std::string db_type_names[] = { "MSSQL", "ORACLE&nbsp;&nbsp;", "MYSQL", "OTHER", "", "" };

struct db_candidate_t
{
    int stage;
    int type;
    std::string name;
    std::string conn;
    std::string bpath;
    std::string dpath;

};

static db_candidate_t db_candidate;

extern void mssql_add_db(const std::string &n, const std::string &c, const std::string &t, const std::string &r,
            size_t p, size_t s0, size_t s1, int m);
extern void oracle_add_db(const std::string &n, const std::string &c, const std::string &t, const std::string &r,
            size_t p, size_t s0, size_t s1, int m);
extern void dirs_add_dir(std::string d);

static void conf_dbs_create()
{
    switch (db_candidate.type) {
    case DB_TYPE_MSSQL: {
        std::string file_temp = props::normalize_path(db_candidate.dpath) + "/" + db_candidate.name;
        mssql_add_db(db_candidate.name, db_candidate.conn, file_temp, file_temp + "_rest", 900, 7*3600, 23*3600, 3);
        ccrules_add_rule(file_temp + "*", wait_since_newest_mod, 100000);
        ccrules_add_rule("*.trn", reconst, 1);
    } break;
    case DB_TYPE_ORACLE: {
        oracle_add_db(db_candidate.name, db_candidate.conn, db_candidate.dpath, "", 900, 7*3600, 23*3600, 3);
        dirs_add_dir(db_candidate.bpath);
        ccrules_add_rule(props::normalize_path(db_candidate.dpath), wait_since_newest_mod, 100000);
    } break;
    default:
        ccrules_add_rule(props::normalize_path(db_candidate.dpath), wait_since_last_copy, 900000);
    }
    dirs_add_dir(db_candidate.dpath);

}


static const std::string form_head = "<center><form action=dbs method=get>";
static const std::string form_tail = "&nbsp;&nbsp;<input type=submit value=\"&gt; &gt; &gt;\" /></form></center>";

static std::string conf_dbs_print_stage()
{
    std::string s = "<p class=\"wizard\"> - ";
    db_candidate.stage++;
    for (int i = 1; i < 7; i++) {
        if (i == db_candidate.stage) s += "<strong class=\"wizard\">";
        s += boost::lexical_cast<std::string>(i);
        if (i == db_candidate.stage) s += "</strong>";
        s += " - ";
    }
    s += "</p><br/>\n";
    return s;
}

std::string conf_dbs_main()
{
    db_candidate.stage = 0;
    std::string s = wserver_pages_head() + shead + conf_dbs_print_stage() + form_head;
    s += "<b>Nombre: </b>&nbsp;&nbsp;<input type=text name=dbn size=20 />";
    s += form_tail + stail;
    return s;
}

std::string conf_dbs_new_db(std::string dbn)
{
    db_candidate.name = dbn;
    std::string s = wserver_pages_head() + shead + conf_dbs_print_stage() + form_head;
    s += "<b>Tipo: </b>&nbsp;&nbsp;&nbsp;&nbsp;<select name=dbt >";
    for (int i = 0; i < DB_TYPE_LAST; i++) {
        s += "<option value=" + boost::lexical_cast<std::string>(i) + ">" + db_type_names[i] + "</option>";
    }
    s += "</select>&nbsp;&nbsp;&nbsp;&nbsp;";
    s += form_tail + stail;
    return s;
}

std::string conf_dbs_dbdir(std::string dir)
{
    db_candidate.bpath = dir;
    std::string s = wserver_pages_head() + shead + conf_dbs_print_stage() + form_head;
    std::string exampl = "<seleccionar una ruta>";
    if (db_candidate.type > DB_TYPE_ORACLE) exampl = "<seleccionar la ruta de los ficheros de datos>";
    s += "<b>Ruta datos: </b><br/><input type=text name=dird size=60 placeholder=\"" + exampl + "\" /><br/>\n";
    s += form_tail + stail;
    return s;
}
std::string conf_dbs_dbconn(std::string conn)
{
    db_candidate.conn = conn;
    std::string s = wserver_pages_head() + shead + conf_dbs_print_stage() + form_head;
    if (db_candidate.type != DB_TYPE_ORACLE) return conf_dbs_dbdir("");
    s += "<b>Ruta instancia: </b><br/><input type=text name=dir size=60 /><br/>";
    s += form_tail + stail;
    return s;
}

std::string conf_dbs_dbtype(std::string dbt)
{
    db_candidate.type = atoi(dbt.c_str());
    std::string s = wserver_pages_head() + shead + conf_dbs_print_stage() + form_head;
    if (db_candidate.type > DB_TYPE_ORACLE) return conf_dbs_dbconn("");
    std::string exampl = "-S &quot;<hostname>:<port>\\<instance>&quot; -E | -U sa -P <sa_pass>";
    if (db_candidate.type == DB_TYPE_ORACLE) exampl = "TARGET SYS/<password>@<instance>";
    s += "<b>Par&aacute;metros de conexi&oacute;n: </b><br/><input type=text name=con size=60 placeholder=\"" + exampl + "\" /><br/>\n";
    s += form_tail + stail;
    return s;
}

std::string conf_dbs_dbdird(std::string dir)
{
    db_candidate.dpath = dir;
    conf_dbs_create();
    std::string s = wserver_pages_head() + shead + conf_dbs_print_stage();
    s += "<b>" + db_candidate.name + "</b> " + db_type_names[db_candidate.type] + " configurada";
    s += stail;
    return s;
}


std::string conf_dbs_dispatch(const std::vector<std::string> &vuri)
{
    if (vuri.size() > 2) {
        if (vuri[1] == "dbn") {
            return conf_dbs_new_db(web_url_decode(vuri[2]));
        } else if (vuri[1] == "dbt") {
            return conf_dbs_dbtype(web_url_decode(vuri[2]));
        } else if (vuri[1] == "con") {
            return conf_dbs_dbconn(web_url_decode(vuri[2]));
        } else if (vuri[1] == "dir") {
            return conf_dbs_dbdir(web_url_decode(vuri[2]));
        } else if (vuri[1] == "dird") {
            return conf_dbs_dbdird(web_url_decode(vuri[2]));
        }
    }

    return conf_dbs_main();
}
