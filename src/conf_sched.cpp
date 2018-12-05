
#include "props_sched.h"
#include "web_utils.h"

static const std::string shead = "<body><h2>Configuraci&oacute;n</h2>\n"
    "<a href=/ class=ml>home</a><a href=/menu class=ml>menu</a><a href=/dict class=ml>diccionario</a><br/>\n";
static const std::string stail = "<hr/><a href=/ class=ml>home</a><a href=/menu class=ml>menu</a>"
    "<a href=/ptasks class=ml>tareas prog.</a>"
    "</body></html>";

extern std::string dirs_main();
extern std::string rules_main();

std::string conf_main()
{
    std::string s = wserver_pages_head() + shead;
    s += "<h3><form onsubmit=\"return confirm('Do you really want to change backend?')\""
            " action=\"/conf\">Backend: "
        "<select name=\"backend\">"
        "<option value=\"local\"";
        if (props::get().backend == "local") s += " selected";
        s += ">LOCAL</option><option value=\"ssh\"";
        if (props::get().backend == "ssh") s += " selected";
        s += ">SSH</option></select>\n"
        "<input type=\"submit\" value=SET></form></h3><br/>\n";
    s += dirs_main();
    s += rules_main();
    s += stail;
    return s;
}

extern void sched_reset();
std::string conf_dispatch(const std::vector<std::string> &vuri)
{
    if (vuri.size() > 2) {
        if (vuri[1] == "backend") {
            if (props::get().update(vuri[2]))
                sched_reset();
        }
    }

    return conf_main();
}
