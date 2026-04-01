#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

/* ─── Constantes ──────────────────────────────────────────────── */
#define MAX_SENSORS      256
#define MAX_OPERATORS     64
#define MAX_ALERTS       512
#define MAX_READINGS    2048
#define BUFFER_SIZE     4096
#define MAX_FIELD_LEN    128

/* ─── Umbrales de alerta ──────────────────────────────────────── */
/* temperature: >80 °C  o  <-10 °C                               */
/* vibration  : >7.5 Hz                                           */
/* energy     : >800 W                                            */
/* humidity   : >90 %                                             */
#define THRESH_TEMP_HIGH   80.0f
#define THRESH_TEMP_LOW   -10.0f
#define THRESH_VIB_HIGH    7.5f
#define THRESH_ENERGY_HIGH 800.0f
#define THRESH_HUMIDITY_HIGH 90.0f

/* ─── Estructuras ─────────────────────────────────────────────── */

typedef struct {
    char id[MAX_FIELD_LEN];
    char type[MAX_FIELD_LEN];
    char location[MAX_FIELD_LEN];
    float last_value;
    char last_unit[32];
    char last_ts[32];
    int  active;
    int  fd;           /* socket del sensor (para referencia) */
} Sensor;

typedef struct {
    char id[MAX_FIELD_LEN];
    char username[MAX_FIELD_LEN];
    char role[32];
    int  active;
    int  fd;
} Operator;

typedef struct {
    char sensor_id[MAX_FIELD_LEN];
    char type[MAX_FIELD_LEN];
    char message[256];
    float value;
    time_t ts;
} Alert;

typedef struct {
    char sensor_id[MAX_FIELD_LEN];
    float value;
    char unit[32];
    time_t ts;
} Reading;

typedef struct {
    int  fd;
    char ip[INET6_ADDRSTRLEN];
    int  port;
} ClientCtx;

/* ─── Estado global ───────────────────────────────────────────── */
static Sensor   g_sensors[MAX_SENSORS];
static int      g_nsensors = 0;
static pthread_mutex_t g_sensors_mtx = PTHREAD_MUTEX_INITIALIZER;

static Operator g_operators[MAX_OPERATORS];
static int      g_noperators = 0;
static pthread_mutex_t g_operators_mtx = PTHREAD_MUTEX_INITIALIZER;

static Alert    g_alerts[MAX_ALERTS];
static int      g_nalerts = 0;
static pthread_mutex_t g_alerts_mtx = PTHREAD_MUTEX_INITIALIZER;

static Reading  g_readings[MAX_READINGS];
static int      g_nreadings = 0;
static pthread_mutex_t g_readings_mtx = PTHREAD_MUTEX_INITIALIZER;

static FILE     *g_logfp  = NULL;
static pthread_mutex_t g_log_mtx = PTHREAD_MUTEX_INITIALIZER;

static volatile int g_running = 1;

/* ─── Logging ─────────────────────────────────────────────────── */
static void log_entry(const char *ip, int port,
                      const char *rx, const char *tx)
{
    time_t now = time(NULL);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));

    pthread_mutex_lock(&g_log_mtx);

    fprintf(stdout,
            "[%s] IP=%-18s PORT=%-6d RX=%-55s TX=%s\n",
            ts, ip, port,
            rx ? rx : "-",
            tx ? tx : "-");
    fflush(stdout);

    if (g_logfp) {
        fprintf(g_logfp,
                "[%s] IP=%-18s PORT=%-6d RX=%-55s TX=%s\n",
                ts, ip, port,
                rx ? rx : "-",
                tx ? tx : "-");
        fflush(g_logfp);
    }

    pthread_mutex_unlock(&g_log_mtx);
}

/* ─── Envío seguro ────────────────────────────────────────────── */
static int send_msg(int fd, const char *msg)
{
    return (int)send(fd, msg, strlen(msg), MSG_NOSIGNAL);
}

/* ─── Split de campos separados por '|' ──────────────────────── */
/*
 * Divide 'line' por '|' y guarda punteros en fields[].
 * Modifica 'line' in-place (reemplaza '|' con '\0').
 * Devuelve el número de campos encontrados.
 */
static int split_fields(char *line, char *fields[], int max_fields)
{
    int n = 0;
    char *p = line;
    while (p && n < max_fields) {
        fields[n++] = p;
        p = strchr(p, '|');
        if (p) *p++ = '\0';
    }
    return n;
}

/* ─── Broadcast a todos los operadores conectados ────────────── */
static void broadcast_operators(const char *msg)
{
    pthread_mutex_lock(&g_operators_mtx);
    for (int i = 0; i < g_noperators; i++) {
        if (g_operators[i].active && g_operators[i].fd >= 0)
            send_msg(g_operators[i].fd, msg);
    }
    pthread_mutex_unlock(&g_operators_mtx);
}

/* ─── Detección de anomalías ──────────────────────────────────── */
static void check_anomaly(const char *sensor_id, const char *stype,
                          float value, const char *unit,
                          const char *ip, int port)
{
    char alert_msg[512] = {0};
    int  anomaly = 0;

    if (strcmp(stype, "temperature") == 0) {
        if (value > THRESH_TEMP_HIGH) {
            snprintf(alert_msg, sizeof(alert_msg),
                "ALERT|HIGH_TEMP|%s|%.2f|%s|threshold=%.1f",
                sensor_id, value, unit, THRESH_TEMP_HIGH);
            anomaly = 1;
        } else if (value < THRESH_TEMP_LOW) {
            snprintf(alert_msg, sizeof(alert_msg),
                "ALERT|LOW_TEMP|%s|%.2f|%s|threshold=%.1f",
                sensor_id, value, unit, THRESH_TEMP_LOW);
            anomaly = 1;
        }
    } else if (strcmp(stype, "vibration") == 0) {
        if (value > THRESH_VIB_HIGH) {
            snprintf(alert_msg, sizeof(alert_msg),
                "ALERT|HIGH_VIBRATION|%s|%.2f|%s|threshold=%.1f",
                sensor_id, value, unit, THRESH_VIB_HIGH);
            anomaly = 1;
        }
    } else if (strcmp(stype, "energy") == 0) {
        if (value > THRESH_ENERGY_HIGH) {
            snprintf(alert_msg, sizeof(alert_msg),
                "ALERT|HIGH_ENERGY|%s|%.2f|%s|threshold=%.1f",
                sensor_id, value, unit, THRESH_ENERGY_HIGH);
            anomaly = 1;
        }
    } else if (strcmp(stype, "humidity") == 0) {
        if (value > THRESH_HUMIDITY_HIGH) {
            snprintf(alert_msg, sizeof(alert_msg),
                "ALERT|HIGH_HUMIDITY|%s|%.2f|%s|threshold=%.1f",
                sensor_id, value, unit, THRESH_HUMIDITY_HIGH);
            anomaly = 1;
        }
    } else if (strcmp(stype, "operational") == 0) {
        /* Para sensores operacionales el valor es un string (FAIL, CRITICAL) */
        /* Se detecta anomalía si unit es "state" y value > 0 (codificado) */
        /* Los clientes mandan value=0 para FAIL/CRITICAL — se maneja abajo */
    }

    if (anomaly) {
        /* Guardar alerta */
        pthread_mutex_lock(&g_alerts_mtx);
        if (g_nalerts < MAX_ALERTS) {
            Alert *a = &g_alerts[g_nalerts++];
            strncpy(a->sensor_id, sensor_id, sizeof(a->sensor_id) - 1);
            strncpy(a->type,      stype,     sizeof(a->type) - 1);
            strncpy(a->message,   alert_msg, sizeof(a->message) - 1);
            a->value = value;
            a->ts    = time(NULL);
        }
        pthread_mutex_unlock(&g_alerts_mtx);

        /* Notificar operadores */
        char notif[600];
        snprintf(notif, sizeof(notif), "%s\n", alert_msg);
        broadcast_operators(notif);

        log_entry(ip, port, NULL, notif);
    }
}

/* ─── Autenticación HTTP hacia el auth-service (Flask) ────────── */
/*
 * El auth-service corre en Python/Flask y expone:
 *   POST /auth  body: {"username":"x","password":"y"}
 *
 * Esta función construye manualmente la petición HTTP/1.1
 * usando sockets raw (sin libcurl ni libssl) para no tener
 * dependencias externas en el servidor C.
 */
static int auth_http(const char *username, const char *password,
                     char *role_out, int role_len)
{
    const char *host = getenv("AUTH_HOST") ? getenv("AUTH_HOST") : "localhost";
    const char *port = getenv("AUTH_PORT") ? getenv("AUTH_PORT") : "5000";

    /* Resolver hostname */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        fprintf(stderr, "[AUTH] DNS error for %s:%s\n", host, port);
        /* Modo dev: acepta admin/admin sin auth service */
        if (strcmp(username, "admin")  == 0 && strcmp(password, "admin")  == 0)
            { strncpy(role_out, "admin",    role_len); return 1; }
        if (strcmp(username, "sara")   == 0 && strcmp(password, "1234")   == 0)
            { strncpy(role_out, "operator", role_len); return 1; }
        return 0;
    }

    int sock = socket(res->ai_family, SOCK_STREAM, 0);
    if (sock < 0) { freeaddrinfo(res); return 0; }

    /* Timeout de 3 segundos para no bloquear el servidor */
    struct timeval tv = {3, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res); close(sock);
        fprintf(stderr, "[AUTH] Could not connect to %s:%s\n", host, port);
        /* Fallback dev */
        if (strcmp(username, "admin") == 0 && strcmp(password, "admin") == 0)
            { strncpy(role_out, "admin",    role_len); return 1; }
        if (strcmp(username, "sara")  == 0 && strcmp(password, "1234")  == 0)
            { strncpy(role_out, "operator", role_len); return 1; }
        return 0;
    }
    freeaddrinfo(res);

    /* Construir JSON body */
    char body[256];
    snprintf(body, sizeof(body),
             "{\"username\":\"%s\",\"password\":\"%s\"}",
             username, password);

    /* Construir petición HTTP/1.1 */
    char req[1024];
    snprintf(req, sizeof(req),
             "POST /auth HTTP/1.1\r\n"
             "Host: %s:%s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             host, port, strlen(body), body);

    send(sock, req, strlen(req), 0);

    /* Leer respuesta completa */
    char resp[2048] = {0};
    ssize_t total = 0;
    ssize_t n;
    while ((n = recv(sock, resp + total, sizeof(resp) - 1 - total, 0)) > 0)
        total += n;
    close(sock);

    /* Buscar el status HTTP */
    if (!strstr(resp, "200 OK")) return 0;

    /* Extraer role del JSON: busca "role":"xxx" */
    char *p = strstr(resp, "\"role\"");
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '"') p++;
    int i = 0;
    while (*p && *p != '"' && *p != '}' && *p != '\n' && i < role_len - 1)
        role_out[i++] = *p++;
    role_out[i] = '\0';

    return (i > 0) ? 1 : 0;
}

/* ─── Procesamiento de mensajes del protocolo pipe ────────────── */
/*
 * Cada línea recibida se analiza aquí.
 * El protocolo usa '|' como separador de campos.
 *
 * Mensajes entrantes (desde sensores):
 *   REGISTER|<id>|<type>|<location>
 *   DATA|<id>|<type>|<value>|<unit>|<timestamp>
 *   HEARTBEAT|<id>|<timestamp>
 *   QUIT|<id>
 *
 * Mensajes de operadores (futuros clientes):
 *   AUTH_OPERATOR|<op_id>|<username>|<password>
 *   STATUS
 *   LIST_SENSORS
 *   LIST_ALERTS
 *
 * Respuestas del servidor:
 *   OK REGISTERED\n
 *   OK DATA\n
 *   OK HEARTBEAT\n
 *   OK BYE\n
 *   ERROR|<motivo>\n
 *   ALERT|<tipo>|<sensor_id>|<value>|<unit>|<detalle>\n
 */
static void process_message(const char *ip, int port, int fd,
                             char *line, char *resp_buf, int resp_len)
{
    /* Copiar para no destruir el original antes del log */
    char work[BUFFER_SIZE];
    strncpy(work, line, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    char *fields[16];
    memset(fields, 0, sizeof(fields));
    int nf = split_fields(work, fields, 16);

    if (nf == 0 || fields[0] == NULL) {
        snprintf(resp_buf, resp_len, "ERROR|EMPTY_MESSAGE\n");
        return;
    }

    const char *cmd = fields[0];

    /* ── REGISTER ── */
    if (strcmp(cmd, "REGISTER") == 0) {
        if (nf < 4) {
            snprintf(resp_buf, resp_len, "ERROR|INVALID_REGISTER\n");
            return;
        }
        const char *sid  = fields[1];
        const char *stype= fields[2];
        const char *sloc = fields[3];

        pthread_mutex_lock(&g_sensors_mtx);
        Sensor *target = NULL;
        for (int i = 0; i < g_nsensors; i++) {
            if (strcmp(g_sensors[i].id, sid) == 0) {
                target = &g_sensors[i];
                break;
            }
        }
        if (!target && g_nsensors < MAX_SENSORS) {
            target = &g_sensors[g_nsensors++];
            memset(target, 0, sizeof(*target));
        }
        if (target) {
            strncpy(target->id,       sid,   sizeof(target->id) - 1);
            strncpy(target->type,     stype, sizeof(target->type) - 1);
            strncpy(target->location, sloc,  sizeof(target->location) - 1);
            target->active = 1;
            target->fd     = fd;
        }
        pthread_mutex_unlock(&g_sensors_mtx);

        snprintf(resp_buf, resp_len, "OK REGISTERED\n");
        return;
    }

    /* ── DATA ── */
    if (strcmp(cmd, "DATA") == 0) {
        /*  DATA|<id>|<type>|<value>|<unit>|<timestamp>  */
        if (nf < 6) {
            snprintf(resp_buf, resp_len, "ERROR|INVALID_DATA\n");
            return;
        }
        const char *sid   = fields[1];
        const char *stype = fields[2];
        const char *sval  = fields[3];
        const char *sunit = fields[4];
        const char *sts   = fields[5];

        float value = 0.0f;
        int   is_numeric = 1;

        /* Para sensores de estado (operational) el valor puede ser "OK","FAIL"... */
        if (sscanf(sval, "%f", &value) != 1) {
            is_numeric = 0;
            /* Codificar estado: FAIL/CRITICAL = -1, WARNING = 0.5, OK = 0 */
            if (strcmp(sval, "FAIL") == 0 || strcmp(sval, "CRITICAL") == 0)
                value = -1.0f;
            else if (strcmp(sval, "WARNING") == 0)
                value = 0.5f;
        }

        /* Actualizar sensor */
        pthread_mutex_lock(&g_sensors_mtx);
        for (int i = 0; i < g_nsensors; i++) {
            if (strcmp(g_sensors[i].id, sid) == 0) {
                g_sensors[i].last_value = value;
                strncpy(g_sensors[i].last_unit, sunit, sizeof(g_sensors[i].last_unit)-1);
                strncpy(g_sensors[i].last_ts,   sts,   sizeof(g_sensors[i].last_ts)-1);
                g_sensors[i].active = 1;
                break;
            }
        }
        pthread_mutex_unlock(&g_sensors_mtx);

        /* Guardar lectura */
        pthread_mutex_lock(&g_readings_mtx);
        if (g_nreadings < MAX_READINGS) {
            Reading *r = &g_readings[g_nreadings++];
            strncpy(r->sensor_id, sid,   sizeof(r->sensor_id) - 1);
            r->value = value;
            strncpy(r->unit, sunit, sizeof(r->unit) - 1);
            r->ts = time(NULL);
        }
        pthread_mutex_unlock(&g_readings_mtx);

        /* Notificar operadores con la medición */
        char notif[256];
        snprintf(notif, sizeof(notif),
                 "DATA|%s|%s|%s|%s|%s\n", sid, stype, sval, sunit, sts);
        broadcast_operators(notif);

        /* Verificar anomalía (solo para sensores numéricos) */
        if (is_numeric)
            check_anomaly(sid, stype, value, sunit, ip, port);

        /* Para operational con FAIL/CRITICAL, generar alerta directa */
        if (!is_numeric &&
            (strcmp(sval, "FAIL") == 0 || strcmp(sval, "CRITICAL") == 0)) {
            char alert_msg[512];
            snprintf(alert_msg, sizeof(alert_msg),
                     "ALERT|OPERATIONAL_FAIL|%s|%s|%s\n",
                     sid, sval, sts);
            broadcast_operators(alert_msg);

            pthread_mutex_lock(&g_alerts_mtx);
            if (g_nalerts < MAX_ALERTS) {
                Alert *a = &g_alerts[g_nalerts++];
                strncpy(a->sensor_id, sid, sizeof(a->sensor_id) - 1);
                strncpy(a->type, "operational", sizeof(a->type) - 1);
                strncpy(a->message, alert_msg, sizeof(a->message) - 1);
                a->value = value;
                a->ts = time(NULL);
            }
            pthread_mutex_unlock(&g_alerts_mtx);
        }

        snprintf(resp_buf, resp_len, "OK DATA\n");
        return;
    }

    /* ── HEARTBEAT ── */
    if (strcmp(cmd, "HEARTBEAT") == 0) {
        /* HEARTBEAT|<sensor_id>|<timestamp> */
        if (nf >= 2) {
            /* Marcar sensor como activo */
            pthread_mutex_lock(&g_sensors_mtx);
            for (int i = 0; i < g_nsensors; i++) {
                if (strcmp(g_sensors[i].id, fields[1]) == 0) {
                    g_sensors[i].active = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&g_sensors_mtx);
        }
        snprintf(resp_buf, resp_len, "OK HEARTBEAT\n");
        return;
    }

    /* ── QUIT ── */
    if (strcmp(cmd, "QUIT") == 0) {
        if (nf >= 2) {
            pthread_mutex_lock(&g_sensors_mtx);
            for (int i = 0; i < g_nsensors; i++) {
                if (strcmp(g_sensors[i].id, fields[1]) == 0) {
                    g_sensors[i].active = 0;
                    g_sensors[i].fd     = -1;
                    break;
                }
            }
            pthread_mutex_unlock(&g_sensors_mtx);
        }
        snprintf(resp_buf, resp_len, "OK BYE\n");
        return;
    }

    /* ── AUTH_OPERATOR (para el cliente operador) ── */
    /* AUTH_OPERATOR|<op_id>|<username>|<password>   */
    if (strcmp(cmd, "AUTH_OPERATOR") == 0) {
        if (nf < 4) {
            snprintf(resp_buf, resp_len, "ERROR|INVALID_AUTH\n");
            return;
        }
        const char *op_id    = fields[1];
        const char *username = fields[2];
        const char *password = fields[3];

        char role[64] = {0};
        if (!auth_http(username, password, role, sizeof(role))) {
            snprintf(resp_buf, resp_len, "ERROR|AUTH_FAILED\n");
            return;
        }

        pthread_mutex_lock(&g_operators_mtx);
        Operator *target = NULL;
        for (int i = 0; i < g_noperators; i++) {
            if (strcmp(g_operators[i].id, op_id) == 0) {
                target = &g_operators[i]; break;
            }
        }
        if (!target && g_noperators < MAX_OPERATORS) {
            target = &g_operators[g_noperators++];
            memset(target, 0, sizeof(*target));
        }
        if (target) {
            strncpy(target->id,       op_id,    sizeof(target->id) - 1);
            strncpy(target->username, username, sizeof(target->username) - 1);
            strncpy(target->role,     role,     sizeof(target->role) - 1);
            target->active = 1;
            target->fd     = fd;
        }
        pthread_mutex_unlock(&g_operators_mtx);

        snprintf(resp_buf, resp_len, "OK AUTH_OPERATOR|%s|%s\n", op_id, role);
        return;
    }

    /* ── STATUS ── */
    if (strcmp(cmd, "STATUS") == 0) {
        int as = 0, ao = 0;
        pthread_mutex_lock(&g_sensors_mtx);
        for (int i = 0; i < g_nsensors; i++) if (g_sensors[i].active) as++;
        pthread_mutex_unlock(&g_sensors_mtx);
        pthread_mutex_lock(&g_operators_mtx);
        for (int i = 0; i < g_noperators; i++) if (g_operators[i].active) ao++;
        pthread_mutex_unlock(&g_operators_mtx);
        pthread_mutex_lock(&g_alerts_mtx);
        int na = g_nalerts;
        pthread_mutex_unlock(&g_alerts_mtx);

        snprintf(resp_buf, resp_len,
                 "STATUS|sensors=%d|operators=%d|alerts=%d\n",
                 as, ao, na);
        return;
    }

    /* ── LIST_SENSORS ── */
    if (strcmp(cmd, "LIST_SENSORS") == 0) {
        char buf[BUFFER_SIZE];
        int pos = snprintf(buf, sizeof(buf), "SENSORS");
        pthread_mutex_lock(&g_sensors_mtx);
        for (int i = 0; i < g_nsensors && pos < BUFFER_SIZE - 128; i++) {
            if (!g_sensors[i].active) continue;
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            "|%s,%s,%s,%.2f,%s",
                            g_sensors[i].id,
                            g_sensors[i].type,
                            g_sensors[i].location,
                            g_sensors[i].last_value,
                            g_sensors[i].last_unit);
        }
        pthread_mutex_unlock(&g_sensors_mtx);
        snprintf(buf + pos, sizeof(buf) - pos, "\n");
        strncpy(resp_buf, buf, resp_len - 1);
        return;
    }

    /* ── LIST_ALERTS ── */
    if (strcmp(cmd, "LIST_ALERTS") == 0) {
        char buf[BUFFER_SIZE];
        int pos = snprintf(buf, sizeof(buf), "ALERTS");
        pthread_mutex_lock(&g_alerts_mtx);
        int start = (g_nalerts > 20) ? g_nalerts - 20 : 0;
        for (int i = start; i < g_nalerts && pos < BUFFER_SIZE - 256; i++) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            "|%s,%s,%.2f",
                            g_alerts[i].sensor_id,
                            g_alerts[i].type,
                            g_alerts[i].value);
        }
        pthread_mutex_unlock(&g_alerts_mtx);
        snprintf(buf + pos, sizeof(buf) - pos, "\n");
        strncpy(resp_buf, buf, resp_len - 1);
        return;
    }

    /* ── Comando desconocido ── */
    snprintf(resp_buf, resp_len, "ERROR|UNKNOWN_COMMAND\n");
}

/* ─── Servidor HTTP mínimo ────────────────────────────────────── */
static void handle_http(ClientCtx *ctx, const char *request)
{
    char method[8], path[256];
    sscanf(request, "%7s %255s", method, path);

    int as = 0, ao = 0, na = 0;
    pthread_mutex_lock(&g_sensors_mtx);
    for (int i = 0; i < g_nsensors; i++) if (g_sensors[i].active) as++;
    pthread_mutex_unlock(&g_sensors_mtx);
    pthread_mutex_lock(&g_operators_mtx);
    for (int i = 0; i < g_noperators; i++) if (g_operators[i].active) ao++;
    pthread_mutex_unlock(&g_operators_mtx);
    pthread_mutex_lock(&g_alerts_mtx);
    na = g_nalerts;
    pthread_mutex_unlock(&g_alerts_mtx);

    char body[4096];
    const char *ctype;

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        ctype = "text/html; charset=UTF-8";
        snprintf(body, sizeof(body),
            "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
            "<title>IoT Monitor</title>"
            "<style>"
            "body{font-family:monospace;background:#0d1117;color:#c9d1d9;margin:0;padding:2rem}"
            "h1{color:#58a6ff}table{border-collapse:collapse;width:100%%}"
            "td,th{border:1px solid #30363d;padding:.5rem 1rem;text-align:left}"
            "th{background:#161b22;color:#58a6ff}"
            ".ok{color:#3fb950}.warn{color:#f85149}"
            "</style></head><body>"
            "<h1>&#x1F4E1; IoT Monitoring System</h1>"
            "<table><tr><th>Métrica</th><th>Valor</th></tr>"
            "<tr><td>Sensores activos</td><td class='ok'>%d</td></tr>"
            "<tr><td>Operadores conectados</td><td class='ok'>%d</td></tr>"
            "<tr><td>Alertas generadas</td><td class='%s'>%d</td></tr>"
            "</table>"
            "<p style='color:#8b949e;margin-top:2rem'>"
            "API: <a href='/api/status' style='color:#58a6ff'>/api/status</a> · "
            "<a href='/api/sensors' style='color:#58a6ff'>/api/sensors</a> · "
            "<a href='/api/alerts'  style='color:#58a6ff'>/api/alerts</a></p>"
            "</body></html>",
            as, ao, na > 0 ? "warn" : "ok", na);

    } else if (strcmp(path, "/api/status") == 0) {
        ctype = "application/json";
        snprintf(body, sizeof(body),
                 "{\"sensors\":%d,\"operators\":%d,\"alerts\":%d}",
                 as, ao, na);

    } else if (strcmp(path, "/api/sensors") == 0) {
        ctype = "application/json";
        int pos = snprintf(body, sizeof(body), "[");
        int first = 1;
        pthread_mutex_lock(&g_sensors_mtx);
        for (int i = 0; i < g_nsensors; i++) {
            if (!g_sensors[i].active) continue;
            pos += snprintf(body + pos, sizeof(body) - pos,
                "%s{\"id\":\"%s\",\"type\":\"%s\","
                "\"location\":\"%s\",\"value\":%.2f,\"unit\":\"%s\"}",
                first ? "" : ",",
                g_sensors[i].id,
                g_sensors[i].type,
                g_sensors[i].location,
                g_sensors[i].last_value,
                g_sensors[i].last_unit);
            first = 0;
        }
        pthread_mutex_unlock(&g_sensors_mtx);
        snprintf(body + pos, sizeof(body) - pos, "]");

    } else if (strcmp(path, "/api/alerts") == 0) {
        ctype = "application/json";
        int pos = snprintf(body, sizeof(body), "[");
        int first = 1;
        pthread_mutex_lock(&g_alerts_mtx);
        int start = (na > 20) ? na - 20 : 0;
        for (int i = start; i < na; i++) {
            pos += snprintf(body + pos, sizeof(body) - pos,
                "%s{\"sensor\":\"%s\",\"type\":\"%s\",\"value\":%.2f}",
                first ? "" : ",",
                g_alerts[i].sensor_id,
                g_alerts[i].type,
                g_alerts[i].value);
            first = 0;
        }
        pthread_mutex_unlock(&g_alerts_mtx);
        snprintf(body + pos, sizeof(body) - pos, "]");

    } else {
        const char *r404 =
            "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n"
            "Connection: close\r\n\r\nNot Found";
        send_msg(ctx->fd, r404);
        log_entry(ctx->ip, ctx->port, request, "HTTP 404");
        return;
    }

    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n",
             ctype, strlen(body));
    send_msg(ctx->fd, header);
    send_msg(ctx->fd, body);
    log_entry(ctx->ip, ctx->port, request, "HTTP 200");
}

/* ─── Hilo por cliente ────────────────────────────────────────── */
static void *client_thread(void *arg)
{
    ClientCtx *ctx = (ClientCtx *)arg;
    char buffer[BUFFER_SIZE];
    char resp[BUFFER_SIZE];
    char leftover[BUFFER_SIZE] = {0};  /* datos incompletos entre recv */

    log_entry(ctx->ip, ctx->port, "CONNECTED", NULL);

    while (g_running) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t n = recv(ctx->fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        buffer[n] = '\0';

        /* Detectar HTTP en el primer mensaje */
        if (strncmp(buffer, "GET ",  4) == 0 ||
            strncmp(buffer, "POST ", 5) == 0 ||
            strncmp(buffer, "HEAD ", 5) == 0) {
            handle_http(ctx, buffer);
            break;   /* HTTP es sin estado, cerramos tras responder */
        }

        /* Protocolo IoT: puede llegar más de una línea en un recv */
        /* Concatenar con lo que quedó del recv anterior            */
        char combined[BUFFER_SIZE * 2];
        snprintf(combined, sizeof(combined), "%s%s", leftover, buffer);
        leftover[0] = '\0';

        char *line_start = combined;
        char *newline;

        while ((newline = strchr(line_start, '\n')) != NULL) {
            *newline = '\0';

            /* Eliminar \r si existe */
            size_t len = strlen(line_start);
            if (len > 0 && line_start[len - 1] == '\r')
                line_start[len - 1] = '\0';

            if (strlen(line_start) == 0) {
                line_start = newline + 1;
                continue;
            }

            /* Procesar mensaje */
            memset(resp, 0, sizeof(resp));
            process_message(ctx->ip, ctx->port, ctx->fd,
                            line_start, resp, sizeof(resp));

            /* Enviar respuesta */
            if (strlen(resp) > 0)
                send_msg(ctx->fd, resp);

            log_entry(ctx->ip, ctx->port, line_start, resp);

            /* Si fue QUIT, cerrar conexión */
            if (strncmp(resp, "OK BYE", 6) == 0) goto cleanup;

            line_start = newline + 1;
        }

        /* Guardar lo que quedó incompleto (sin \n) */
        if (strlen(line_start) > 0)
            strncpy(leftover, line_start, sizeof(leftover) - 1);
    }

cleanup:
    log_entry(ctx->ip, ctx->port, "DISCONNECTED", NULL);
    close(ctx->fd);
    free(ctx);
    return NULL;
}

/* ─── Señales ─────────────────────────────────────────────────── */
static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
    printf("\n[SERVER] Señal recibida, cerrando...\n");
}

/* ─── Main ────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <puerto> <archivoDeLogs>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *port_str = argv[1];
    const char *log_path = argv[2];

    /* Abrir archivo de logs */
    g_logfp = fopen(log_path, "a");
    if (!g_logfp) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    /* Señales */
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);  /* ignorar broken pipe al escribir en socket cerrado */

    /* Crear socket del servidor usando getaddrinfo (sin IPs hard-coded) */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;       /* IPv4 */
    hints.ai_socktype = SOCK_STREAM;   /* TCP  */
    hints.ai_flags    = AI_PASSIVE;    /* Bind a todas las interfaces */

    int rc = getaddrinfo(NULL, port_str, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return EXIT_FAILURE;
    }

    int server_fd = socket(res->ai_family, res->ai_socktype, 0);
    if (server_fd < 0) { perror("socket"); return EXIT_FAILURE; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind"); return EXIT_FAILURE;
    }
    freeaddrinfo(res);

    if (listen(server_fd, 64) < 0) {
        perror("listen"); return EXIT_FAILURE;
    }

    printf("╔══════════════════════════════════════════╗\n");
    printf("║      IoT Monitoring Server               ║\n");
    printf("║      Protocolo pipe-separated text       ║\n");
    printf("╚══════════════════════════════════════════╝\n");
    printf("  Puerto   : %s\n", port_str);
    printf("  Logs     : %s\n", log_path);
    printf("  Auth     : %s:%s\n",
           getenv("AUTH_HOST") ? getenv("AUTH_HOST") : "localhost",
           getenv("AUTH_PORT") ? getenv("AUTH_PORT") : "5000");
    printf("  HTTP API : http://localhost:%s/\n\n", port_str);

    log_entry("SERVER", atoi(port_str), "SERVER_START", port_str);

    /* Loop principal de aceptación */
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &addr_len);
        if (client_fd < 0) {
            if (g_running) perror("accept");
            continue;
        }

        /* Preparar contexto del cliente */
        ClientCtx *ctx = calloc(1, sizeof(ClientCtx));
        if (!ctx) { close(client_fd); continue; }

        ctx->fd   = client_fd;
        ctx->port = ntohs(client_addr.sin_port);
        inet_ntop(AF_INET, &client_addr.sin_addr,
                  ctx->ip, sizeof(ctx->ip));

        /* Lanzar hilo dedicado (detached — no hace join) */
        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        if (pthread_create(&tid, &attr, client_thread, ctx) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(ctx);
        }
        pthread_attr_destroy(&attr);
    }

    close(server_fd);
    if (g_logfp) fclose(g_logfp);
    printf("[SERVER] Cerrado.\n");
    return EXIT_SUCCESS;
}