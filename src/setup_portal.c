#include "walldisplay/setup_portal.h"

#include "walldisplay/app_config.h"
#include "walldisplay/wifi_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "sdkconfig.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

/* HTTP setup portal and captive DNS served to clients of the setup AP.
 * wifi_manager owns the AP, its password, and its idle timeout, and provides
 * the connection tests; this module owns the page, the form handling, and the
 * rules for what a portal client may read or change. */

static const char *TAG = "setup_portal";
static esp_netif_t *s_ap_netif;
static httpd_handle_t s_portal;
static TaskHandle_t s_dns_task;
static atomic_bool s_dns_stop;
static int s_portal_attempts;
static int64_t s_portal_window_us;
static SemaphoreHandle_t s_scan_mutex;

static void portal_activity(void) {
    wifi_manager_ap_activity();
}

#define DNS_RECEIVE_TIMEOUT_MS 500

static void dns_task(void *arg) {
    /* The task owns its socket and closes it itself: closing a socket from
     * another task while this one is blocked in recvfrom() is not safe in
     * lwIP and could leave it reading a reused descriptor. */
    const int dns_socket = (int)(intptr_t)arg;
    uint8_t packet[256];
    while (!atomic_load(&s_dns_stop)) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int length = recvfrom(dns_socket, packet, sizeof(packet), 0, (struct sockaddr *)&peer, &peer_len);
        if (length < 12 || length + 16 > (int)sizeof(packet)) continue;
        packet[length++] = 0xc0; packet[length++] = 0x0c;
        packet[length++] = 0x00; packet[length++] = 0x01;
        packet[length++] = 0x00; packet[length++] = 0x01;
        packet[length++] = 0x00; packet[length++] = 0x00; packet[length++] = 0x00; packet[length++] = 0x3c;
        packet[length++] = 0x00; packet[length++] = 0x04;
        packet[length++] = 192; packet[length++] = 168; packet[length++] = 4; packet[length++] = 1;
        packet[2] = 0x81; packet[3] = 0x80; packet[6] = 0; packet[7] = 1;
        sendto(dns_socket, packet, length, 0, (struct sockaddr *)&peer, peer_len);
    }
    close(dns_socket);
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

static void dns_start(void) {
    if (s_dns_task != NULL) return;
    const int dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_socket < 0) return;
    /* A bounded receive lets the task notice a stop request by itself. */
    const struct timeval timeout = {.tv_sec = 0, .tv_usec = DNS_RECEIVE_TIMEOUT_MS * 1000};
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY)};
    atomic_store(&s_dns_stop, false);
    if (setsockopt(dns_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        bind(dns_socket, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        xTaskCreate(dns_task, "wd_dns", 3072, (void *)(intptr_t)dns_socket, 4, &s_dns_task) != pdPASS) {
        close(dns_socket);
        s_dns_task = NULL;
    }
}

static void dns_stop(void) {
    atomic_store(&s_dns_stop, true);
    for (int i = 0; i < 20 && s_dns_task != NULL; ++i) vTaskDelay(pdMS_TO_TICKS(DNS_RECEIVE_TIMEOUT_MS / 5));
    if (s_dns_task != NULL) ESP_LOGW(TAG, "Setup DNS task did not stop in time");
}

static void html_escape(const char *input, char *output, size_t size) {
    size_t out = 0;
    for (size_t i = 0; input != NULL && input[i] != '\0' && out + 6 < size; ++i) {
        const char *replacement = NULL;
        switch (input[i]) { case '&': replacement = "&amp;"; break; case '<': replacement = "&lt;"; break; case '>': replacement = "&gt;"; break; case '"': replacement = "&quot;"; break; case '\'': replacement = "&#39;"; break; default: break; }
        if (replacement != NULL) { size_t n = strlen(replacement); memcpy(output + out, replacement, n); out += n; }
        else output[out++] = input[i];
    }
    output[out] = '\0';
}

static void json_escape(const char *input, char *output, size_t size) {
    size_t out = 0;
    for (size_t i = 0; input != NULL && input[i] != '\0' && out + 2 < size; ++i) {
        unsigned char c = (unsigned char)input[i];
        if (c == '\\' || c == '"') { output[out++] = '\\'; output[out++] = (char)c; }
        else if (c >= 0x20 && c != 0x7f) output[out++] = (char)c;
    }
    output[out] = '\0';
}

static int hex_digit(unsigned char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static void form_url_decode(char *value) {
    char *read = value, *write = value;
    while (*read != '\0') {
        if (*read == '+') {
            *write++ = ' ';
            read++;
        } else if (read[0] == '%' && read[1] != '\0' && read[2] != '\0') {
            const int high = hex_digit((unsigned char)read[1]);
            const int low = hex_digit((unsigned char)read[2]);
            if (high >= 0 && low >= 0) {
                *write++ = (char)((high << 4) | low);
                read += 3;
            } else {
                *write++ = *read++;
            }
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

/* Read one form field. The encoded value is extracted into scratch, which is
 * as large as the request body and therefore cannot truncate, and is decoded
 * before its length is checked against the destination. A value that does not
 * fit is reported through too_long instead of being replaced by a stored one. */
static bool form_field(const char *body, const char *key, char *value, size_t value_size,
                       char *scratch, size_t scratch_size, bool *too_long) {
    value[0] = '\0';
    if (httpd_query_key_value(body, key, scratch, scratch_size) != ESP_OK) return false;
    form_url_decode(scratch);
    if (strlcpy(value, scratch, value_size) >= value_size) {
        value[0] = '\0';
        *too_long = true;
    }
    return true;
}

/* The portal listens on every interface, but only clients of the setup AP may
 * use it: the AP password shown on the display is its access control. Requests
 * that arrived through the station interface are refused. */
static bool portal_request_from_ap(httpd_req_t *req) {
    esp_netif_ip_info_t ap = {0};
    if (s_ap_netif == NULL || esp_netif_get_ip_info(s_ap_netif, &ap) != ESP_OK || ap.ip.addr == 0) return false;
    struct sockaddr_storage local = {0};
    socklen_t local_len = sizeof(local);
    if (getsockname(httpd_req_to_sockfd(req), (struct sockaddr *)&local, &local_len) != 0) return false;
    uint32_t address = 0;
    if (local.ss_family == AF_INET) {
        address = ((const struct sockaddr_in *)&local)->sin_addr.s_addr;
#if CONFIG_LWIP_IPV6
    } else if (local.ss_family == AF_INET6) {
        /* The server socket is dual-stack; IPv4 peers appear IPv4-mapped. */
        static const uint8_t mapped_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        const uint8_t *bytes = ((const struct sockaddr_in6 *)&local)->sin6_addr.s6_addr;
        if (memcmp(bytes, mapped_prefix, sizeof(mapped_prefix)) != 0) return false;
        memcpy(&address, bytes + sizeof(mapped_prefix), sizeof(address));
#endif
    } else {
        return false;
    }
    return address == ap.ip.addr;
}

static esp_err_t portal_forbidden(httpd_req_t *req) {
    httpd_resp_set_status(req, "403 Forbidden");
    return httpd_resp_send(req, "The setup portal is available only through the setup access point", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t portal_get(httpd_req_t *req) {
    if (!portal_request_from_ap(req)) return portal_forbidden(req);
    portal_activity();
    const app_config_t *config = app_config_get();
    char hostname[33] = "walldisplay";
    (void) app_config_display_hostname(config->display_name, hostname, sizeof(hostname));
    char name[APP_DISPLAY_NAME_MAX_LEN * 2 + 1], ssid[APP_WIFI_MAX_SSID_LEN * 2 + 1];
    char mqtt_uri[APP_MQTT_URI_MAX_LEN * 2 + 1], mqtt_user[APP_MQTT_USERNAME_MAX_LEN * 2 + 1];
    char disc_pref[APP_TOPIC_MAX_LEN * 2 + 1], base_topic[APP_TOPIC_MAX_LEN * 2 + 1];
    char topic_prefix[APP_TOPIC_MAX_LEN + 1];
    char generated_topic[APP_TOPIC_MAX_LEN + 1], generated_topic_html[APP_TOPIC_MAX_LEN * 2 + 1];
    const size_t ca_certificate_size = APP_MQTT_CA_MAX_LEN * 2 + 1;
    char *ca_certificate = malloc(ca_certificate_size);
    if (ca_certificate == NULL) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    html_escape(config->display_name, name, sizeof(name)); html_escape(config->wifi_ssid, ssid, sizeof(ssid));
    html_escape(config->mqtt_uri, mqtt_uri, sizeof(mqtt_uri)); html_escape(config->mqtt_username, mqtt_user, sizeof(mqtt_user));
    strlcpy(topic_prefix, config->base_topic, sizeof(topic_prefix));
    char *suffix = strrchr(topic_prefix, '/');
    if (suffix != NULL && strcmp(suffix + 1, hostname) == 0) *suffix = '\0';
    if (app_config_derive_base_topic(topic_prefix, config->display_name, generated_topic, sizeof(generated_topic)) != ESP_OK)
        strlcpy(generated_topic, config->base_topic, sizeof(generated_topic));
    html_escape(generated_topic, generated_topic_html, sizeof(generated_topic_html));
    html_escape(config->discovery_prefix, disc_pref, sizeof(disc_pref)); html_escape(topic_prefix, base_topic, sizeof(base_topic));
    html_escape(config->mqtt_ca_certificate, ca_certificate, ca_certificate_size);
    char *body = malloc(16384);
    if (body == NULL) {
        free(ca_certificate);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    const int body_length = snprintf(body, 16384,
        "<!doctype html><html><head><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>WallDisplay setup</title>"
        "<style>*,*:before,*:after{box-sizing:border-box}html,body{overflow-x:hidden}body{font:16px system-ui,sans-serif;background:#f4f6f8;color:#18212b;margin:0;padding:clamp(10px,4vw,24px)}main{width:100%%;max-width:620px;min-width:0;margin:auto;background:white;padding:clamp(16px,4vw,28px);border-radius:14px;box-shadow:0 4px 18px #0001}h1{margin-top:0;font-size:clamp(1.35rem,5vw,1.8rem)}h2{font-size:1.05rem;border-bottom:1px solid #dde2e7;padding-bottom:8px;margin-top:24px}label{display:block;margin:14px 0 5px;font-weight:600}input,textarea{box-sizing:border-box;width:100%%;min-width:0;padding:10px;border:1px solid #b7c1ca;border-radius:7px;font:inherit}textarea{resize:vertical;max-width:100%%}button{padding:10px 15px;border:0;border-radius:7px;background:#1769aa;color:white;font:inherit;cursor:pointer}button.secondary{background:#e4e9ee;color:#18212b}.row{display:flex;gap:8px;align-items:end;flex-wrap:wrap;min-width:0}.row input{min-width:0;flex:1 1 200px}.row button{white-space:nowrap}.hint{color:#5f6c78;font-size:.9rem}.network{display:flex;justify-content:space-between;gap:8px;padding:9px;border-bottom:1px solid #edf0f2;cursor:pointer;overflow-wrap:anywhere}.network:hover{background:#f0f6fb}.status{display:grid;grid-template-columns:max-content minmax(0,1fr);gap:6px 12px;padding:12px;border-radius:7px;background:#eef5fa;margin:12px 0;overflow-wrap:anywhere}.status b{font-weight:700}.status code{overflow-wrap:anywhere}.error,.success{min-height:1.3em}.error{color:#b42318}.success{color:#167447}@media(max-width:420px){main{border-radius:10px}.row button{flex:1 1 auto}}</style></head><body><main>"
        "<h1>WallDisplay setup</h1><div class=status><b>Display name</b><span>%s</span><b>Hostname</b><code id=hostname>%s</code><b>MQTT topic</b><code id=mqtt_topic>%s</code></div>"
        "<form method=post action=/api/config><h2>Wi-Fi</h2>"
        "<label for=ssid>Network name</label><div class=row><input id=ssid name=ssid maxlength=32 value=\"%s\" required autocapitalize=none autocorrect=off spellcheck=false><button type=button class=secondary onclick=scan()>Refresh networks</button></div><div id=networks class=hint>Choose Refresh networks to scan nearby access points.</div>"
        "<label for=password>Wi-Fi password</label><div class=row><input id=password name=password type=password maxlength=64><button type=button class=secondary onclick=toggle('password',this)>Show</button></div>"
        "<h2>Display</h2><label for=name>Display name</label><input id=name name=name maxlength=64 value=\"%s\" required><p class=hint>Spaces and capitalization are preserved. The generated hostname is shown above.</p>"
        "<h2>MQTT (optional)</h2><label for=mqtt_uri>Broker URI</label><input id=mqtt_uri name=mqtt_uri maxlength=128 value=\"%s\" placeholder= mqtts://broker.example autocapitalize=none autocorrect=off spellcheck=false><label>Username</label><input name=mqtt_user maxlength=64 value=\"%s\" autocapitalize=none autocorrect=off spellcheck=false><label>Password</label><div class=row><input id=mqtt_password name=mqtt_password type=password maxlength=64><button type=button class=secondary onclick=toggle('mqtt_password',this)>Show</button></div>"
        "<label>Discovery prefix</label><input name=disc_pref maxlength=128 value=\"%s\"><label>MQTT topic prefix</label><input id=base_topic name=base_topic maxlength=128 value=\"%s\" autocapitalize=none autocorrect=off spellcheck=false><p class=hint>The panel topic is generated as <code>prefix/hostname</code> from this prefix and the display name.</p><label><input style=width:auto name=mqtt_tls type=checkbox value=ON %s> Require MQTT TLS</label><label for=ca_certificate>Custom MQTT CA certificate (optional)</label><textarea id=ca_certificate name=ca_certificate maxlength=3072 rows=7 placeholder=Leave blank to use the built-in CA bundle>%s</textarea><p class=hint>Paste a PEM certificate, including BEGIN/END CERTIFICATE lines. Leave blank to use the built-in CA bundle.</p><label>Screenshot token (optional)</label><div class=row><input id=screenshot_token name=screenshot_token minlength=32 maxlength=64 pattern=\"[A-Za-z0-9]+\" type=password placeholder=Leave unchanged to keep current token><button type=button class=secondary onclick=generateToken()>Generate</button><button type=button class=secondary onclick=toggle('screenshot_token',this)>Show</button></div><p class=hint>Use 32 to 64 letters and numbers. Leave blank to keep the existing token.</p>"
        "<p id=error class=error role=status aria-live=polite></p><div class=row><button type=submit name=action value=test class=secondary>Test connections</button><button type=submit name=action value=save>Save and reboot</button></div></form><form method=post action=/api/reset onsubmit=\"return confirm('Erase saved settings and restart setup?')\"><button class=secondary type=submit>Erase settings and restart setup</button></form><p class=hint>Test connections checks Wi-Fi and MQTT without saving and reports the result here. Saving validates again and restarts the panel.</p></main>"
        "<script>function toggle(id,b){let e=document.getElementById(id);e.type=e.type==='password'?'text':'password';b.textContent=e.type==='password'?'Show':'Hide'}function hostname(v){return v.toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/^-+|-+$/g,'').slice(0,32)}function updateTopic(){let h=hostname(document.getElementById('name').value),p=document.getElementById('base_topic').value.replace(/\\/+$/,'');document.getElementById('hostname').textContent=h||'(invalid)';document.getElementById('mqtt_topic').textContent=h&&p?p+'/'+h:'(invalid)'}function generateToken(){let a=new Uint8Array(48);crypto.getRandomValues(a);let c='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789',t='';a.forEach(x=>t+=c[x%%c.length]);let e=document.getElementById('screenshot_token');e.value=t;e.type='text'}async function scan(){let n=document.getElementById('networks');n.textContent='Scanning...';try{let r=await fetch('/api/wifi/scan',{cache:'no-store'});if(!r.ok)throw Error();let a=await r.json();n.innerHTML=a.length?'':'No networks found.';a.forEach(x=>{let d=document.createElement('div');d.className='network';d.innerHTML='<span></span><span></span>';d.firstChild.textContent=x.ssid||'(hidden)';d.lastChild.textContent=x.rssi+' dBm - '+x.security;d.onclick=()=>{document.getElementById('ssid').value=x.ssid};n.appendChild(d)})}catch(e){n.textContent='Scan failed. Enter the network name manually.'}}let form=document.querySelector('form');form.addEventListener('submit',async e=>{let status=document.getElementById('error');if(e.submitter&&e.submitter.value==='test'){e.preventDefault();status.className='error';status.textContent='Testing connections...';let params=new URLSearchParams(new FormData(form));params.set('action','test');try{let r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:params});status.textContent=await r.text();status.className=r.ok?'success':'error'}catch(err){status.textContent='Connection test request failed';status.className='error'}}else{status.className='error';status.textContent='Saving...'}});document.getElementById('name').addEventListener('input',updateTopic);document.getElementById('base_topic').addEventListener('input',updateTopic);updateTopic()</script></body></html>",
        name, hostname, generated_topic_html, ssid, name, mqtt_uri, mqtt_user, disc_pref, base_topic, config->mqtt_require_tls ? "checked" : "", ca_certificate);
    if (body_length < 0 || body_length >= 16384) {
        free(body);
        free(ca_certificate);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Setup page too large");
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    esp_err_t result = httpd_resp_send(req, body, body_length);
    free(body);
    free(ca_certificate);
    return result;
}

static esp_err_t portal_scan(httpd_req_t *req) {
    if (!portal_request_from_ap(req)) return portal_forbidden(req);
    portal_activity();
    if (s_scan_mutex == NULL || xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan busy");
    }
    wifi_scan_config_t scan_config = {.scan_type = WIFI_SCAN_TYPE_ACTIVE, .show_hidden = false};
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        xSemaphoreGive(s_scan_mutex);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan unavailable");
    }
    /* The HTTP server task has a small stack. Keep the potentially large AP
     * record array on the heap to avoid corrupting the task stack when a scan
     * is requested from the setup page. */
    wifi_ap_record_t *records = calloc(32, sizeof(*records));
    if (records == NULL) {
        xSemaphoreGive(s_scan_mutex);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan out of memory");
    }
    uint16_t count = 32;
    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        free(records);
        xSemaphoreGive(s_scan_mutex);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan failed");
    }
    httpd_resp_set_type(req, "application/json"); httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "[");
    char item[192], escaped[APP_WIFI_MAX_SSID_LEN * 2 + 1];
    bool emitted = false;
    for (uint16_t i = 0; i < count; ++i) {
        if (records[i].ssid[0] == '\0') continue;
        bool duplicate = false;
        for (uint16_t j = 0; j < i; ++j) if (strcmp((char *)records[i].ssid, (char *)records[j].ssid) == 0) { duplicate = true; break; }
        if (duplicate) continue;
        json_escape((char *)records[i].ssid, escaped, sizeof(escaped));
        const char *security = records[i].authmode == WIFI_AUTH_OPEN ? "Open" : "Secured";
        snprintf(item, sizeof(item), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"security\":\"%s\"}", emitted ? "," : "", escaped, records[i].rssi, security);
        httpd_resp_sendstr_chunk(req, item);
        emitted = true;
    }
    httpd_resp_sendstr_chunk(req, "]");
    err = httpd_resp_sendstr_chunk(req, NULL);
    free(records);
    xSemaphoreGive(s_scan_mutex);
    return err;
}

static esp_err_t portal_status(httpd_req_t *req) {
    if (!portal_request_from_ap(req)) return portal_forbidden(req);
    portal_activity();
    const app_config_t *config = app_config_get();
    char hostname[33] = "walldisplay";
    (void) app_config_display_hostname(config->display_name, hostname, sizeof(hostname));
    char body[192];
    snprintf(body, sizeof(body), "{\"hostname\":\"%s\",\"ap_active\":%s}",
             hostname, wifi_manager_ap_active() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t portal_redirect(httpd_req_t *req) {
    if (!portal_request_from_ap(req)) return portal_forbidden(req);
    portal_activity();
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t portal_post(httpd_req_t *req) {
    if (!portal_request_from_ap(req)) return portal_forbidden(req);
    portal_activity();
    const int64_t now = esp_timer_get_time();
    if (now - s_portal_window_us > 60LL * 1000LL * 1000LL) { s_portal_window_us = now; s_portal_attempts = 0; }
    if (++s_portal_attempts > 5) { httpd_resp_set_status(req, "429 Too Many Requests"); return httpd_resp_send(req, "Try again later", HTTPD_RESP_USE_STRLEN); }
    char content_type[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strncasecmp(content_type, "application/x-www-form-urlencoded", 33) != 0) {
        httpd_resp_set_status(req, "415 Unsupported Media Type");
        return httpd_resp_send(req, "Form encoding required", HTTPD_RESP_USE_STRLEN);
    }
    if (req->content_len <= 0 || req->content_len >= 12288) return httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Request too large");
    const size_t body_size = (size_t)req->content_len + 1;
    char *body = calloc(1, body_size);
    char *scratch = malloc(body_size);
    if (!body || !scratch) {
        free(body);
        free(scratch);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) { free(body); free(scratch); return ESP_FAIL; }
        received += n;
    }
    body[received] = '\0';
    char ssid[APP_WIFI_MAX_SSID_LEN + 1] = {0}, password[APP_WIFI_MAX_PASSWORD_LEN + 1] = {0}, name[APP_DISPLAY_NAME_MAX_LEN + 1] = {0};
    char mqtt_uri[APP_MQTT_URI_MAX_LEN + 1] = {0}, mqtt_user[APP_MQTT_USERNAME_MAX_LEN + 1] = {0}, mqtt_password[APP_MQTT_PASSWORD_MAX_LEN + 1] = {0};
    char disc_pref[APP_TOPIC_MAX_LEN + 1] = {0}, base_topic[APP_TOPIC_MAX_LEN + 1] = {0}, mqtt_tls[8] = {0};
    char screenshot_token[APP_SCREENSHOT_TOKEN_MAX_LEN + 1] = {0};
    char ca_certificate[APP_MQTT_CA_MAX_LEN + 1] = {0};
    char action[16] = {0};
    char hostname[33];
    const app_config_t *current = app_config_get();
    bool too_long = false;
#define PORTAL_FIELD(key, buffer) form_field(body, key, buffer, sizeof(buffer), scratch, body_size, &too_long)
    const bool has_ca = PORTAL_FIELD("ca_certificate", ca_certificate);
    const bool has_password = PORTAL_FIELD("password", password);
    const bool has_mqtt_password = PORTAL_FIELD("mqtt_password", mqtt_password);
    const bool has_ssid = PORTAL_FIELD("ssid", ssid);
    const bool has_name = PORTAL_FIELD("name", name);
    const bool has_mqtt_uri = PORTAL_FIELD("mqtt_uri", mqtt_uri);
    const bool has_mqtt_user = PORTAL_FIELD("mqtt_user", mqtt_user);
    const bool has_disc_pref = PORTAL_FIELD("disc_pref", disc_pref);
    const bool has_base_topic = PORTAL_FIELD("base_topic", base_topic);
    const bool has_screenshot_token = PORTAL_FIELD("screenshot_token", screenshot_token);
    const bool require_tls = PORTAL_FIELD("mqtt_tls", mqtt_tls);
    const bool test_only = PORTAL_FIELD("action", action) && strcmp(action, "test") == 0;
#undef PORTAL_FIELD
    free(scratch);
    free(body);
    if (too_long) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "A submitted value is too long");
    if (!has_ssid || !has_name) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid configuration");
    if (!has_ca) strlcpy(ca_certificate, current->mqtt_ca_certificate, sizeof(ca_certificate));
    if (!has_mqtt_uri) strlcpy(mqtt_uri, current->mqtt_uri, sizeof(mqtt_uri));
    if (!has_mqtt_user) strlcpy(mqtt_user, current->mqtt_username, sizeof(mqtt_user));
    if (!has_disc_pref) strlcpy(disc_pref, current->discovery_prefix, sizeof(disc_pref));
    if (!has_base_topic) strlcpy(base_topic, current->base_topic, sizeof(base_topic));
    if (!has_screenshot_token || screenshot_token[0] == '\0')
        strlcpy(screenshot_token, current->screenshot_token, sizeof(screenshot_token));
    /* A stored secret is reused for an omitted field only when everything that
     * decides who receives it is unchanged. Otherwise a portal client could
     * keep the broker URI, name its own Wi-Fi network or CA certificate, and
     * have the panel present the stored MQTT password to a broker it controls. */
    const bool same_wifi = strcmp(ssid, current->wifi_ssid) == 0;
    const bool same_broker_trust = same_wifi && strcmp(mqtt_uri, current->mqtt_uri) == 0 &&
                                   strcmp(mqtt_user, current->mqtt_username) == 0 &&
                                   strcmp(ca_certificate, current->mqtt_ca_certificate) == 0 &&
                                   (require_tls || !current->mqtt_require_tls);
    if (!has_password && same_wifi) strlcpy(password, current->wifi_password, sizeof(password));
    if (!has_mqtt_password && same_broker_trust) strlcpy(mqtt_password, current->mqtt_password, sizeof(mqtt_password));
    /* The URI may embed credentials; never log its content. */
    ESP_LOGI(TAG, "Portal MQTT URI %s", mqtt_uri[0] != '\0' ? "received" : "empty");
    ESP_LOGI(TAG, "Portal request action: %s", test_only ? "test" : "save");
    char derived_topic[APP_TOPIC_MAX_LEN + 1];
    if (app_config_derive_base_topic(base_topic, name, derived_topic, sizeof(derived_topic)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MQTT topic prefix");
    const esp_err_t wifi_test_err = wifi_manager_test_station(ssid, password);
    const bool wifi_ok = wifi_test_err == ESP_OK;
    const esp_err_t mqtt_test_err = wifi_ok ? wifi_manager_test_mqtt(mqtt_uri, mqtt_user, mqtt_password,
                                                                      require_tls, ca_certificate) : ESP_ERR_INVALID_STATE;
    const bool mqtt_ok = mqtt_test_err == ESP_OK;
    const esp_err_t hostname_err = app_config_display_hostname(name, hostname, sizeof(hostname));
    const esp_err_t config_err = test_only ? ESP_OK :
        ((wifi_ok && mqtt_ok && hostname_err == ESP_OK)
            ? app_config_set_portal_config(ssid, password, name, mqtt_uri, mqtt_user, mqtt_password,
                                           require_tls, disc_pref, derived_topic, screenshot_token,
                                           ca_certificate)
            : ESP_ERR_INVALID_STATE);
    ESP_LOGI(TAG, "Portal save validation: Wi-Fi %s (%s), MQTT %s (%s), hostname %s, config %s",
             wifi_ok ? "ok" : "failed", esp_err_to_name(wifi_test_err),
             mqtt_ok ? "ok" : "failed", esp_err_to_name(mqtt_test_err),
             hostname_err == ESP_OK ? "ok" : "failed", esp_err_to_name(config_err));
    if (!wifi_ok || !mqtt_ok || hostname_err != ESP_OK || config_err != ESP_OK) {
        if (wifi_ok && (test_only || !mqtt_ok || config_err != ESP_OK)) wifi_manager_restore_station();
        const char *message = !wifi_ok ? "Wi-Fi connection failed; previous configuration was kept" :
                              !mqtt_ok ? "MQTT connection failed; previous configuration was kept" :
                              "Could not save configuration; previous configuration was kept";
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, message);
    }
    if (test_only) {
        wifi_manager_restore_station();
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "Wi-Fi and MQTT connections succeeded; nothing was saved.", HTTPD_RESP_USE_STRLEN);
    }
    (void) wifi_manager_set_hostname(hostname);
    httpd_resp_set_type(req, "text/plain");
    esp_err_t result = httpd_resp_send(req, "Saved. Rebooting to connect.", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return result;
}

static esp_err_t portal_reset(httpd_req_t *req) {
    if (!portal_request_from_ap(req)) return portal_forbidden(req);
    portal_activity();
    if (req->content_len != 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unexpected request body");
    const esp_err_t reset_err = app_config_reset_runtime();
    if (reset_err != ESP_OK) {
        char message[96];
        snprintf(message, sizeof(message), "Could not erase settings (%s)", esp_err_to_name(reset_err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, message);
    }
    httpd_resp_set_type(req, "text/plain");
    esp_err_t result = httpd_resp_send(req, "Settings erased. Rebooting to setup mode.", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return result;
}

esp_err_t setup_portal_start(esp_netif_t *ap_netif) {
    if (ap_netif == NULL) return ESP_ERR_INVALID_ARG;
    if (s_scan_mutex == NULL) {
    s_scan_mutex = xSemaphoreCreateMutex();
    if (s_scan_mutex == NULL) return ESP_ERR_NO_MEM;
    }
    s_ap_netif = ap_netif;
    s_portal_attempts = 0;
    s_portal_window_us = esp_timer_get_time();
    if (s_portal != NULL) return ESP_OK;
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    /* The screenshot server uses the HTTPD default control socket. */
    server_config.ctrl_port = 32769;
    /* portal_post keeps bounded form fields on its stack while parsing;
     * allow room for the optional PEM certificate and HTTPD internals. */
    server_config.stack_size = 8192;
    server_config.max_uri_handlers = 12;
    const esp_err_t err = httpd_start(&s_portal, &server_config);
    if (err != ESP_OK) { s_portal = NULL; return err; }
    const httpd_uri_t get_uri = {.uri = "/", .method = HTTP_GET, .handler = portal_get};
    const httpd_uri_t status_uri = {.uri = "/api/status", .method = HTTP_GET, .handler = portal_status};
    const httpd_uri_t scan_uri = {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = portal_scan};
    const httpd_uri_t post_uri = {.uri = "/api/config", .method = HTTP_POST, .handler = portal_post};
    const httpd_uri_t reset_uri = {.uri = "/api/reset", .method = HTTP_POST, .handler = portal_reset};
    const httpd_uri_t redirect_204 = {.uri = "/generate_204", .method = HTTP_GET, .handler = portal_redirect};
    const httpd_uri_t redirect_hotspot = {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = portal_redirect};
    const httpd_uri_t redirect_connect = {.uri = "/connecttest.txt", .method = HTTP_GET, .handler = portal_redirect};
    const httpd_uri_t redirect_ncsi = {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = portal_redirect};
    const esp_err_t get_err = httpd_register_uri_handler(s_portal, &get_uri);
    const esp_err_t status_err = httpd_register_uri_handler(s_portal, &status_uri);
    const esp_err_t scan_err = httpd_register_uri_handler(s_portal, &scan_uri);
    const esp_err_t post_err = httpd_register_uri_handler(s_portal, &post_uri);
    if (get_err != ESP_OK || status_err != ESP_OK || scan_err != ESP_OK || post_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register setup portal API handlers");
    }
    httpd_register_uri_handler(s_portal, &redirect_204); httpd_register_uri_handler(s_portal, &redirect_hotspot);
    httpd_register_uri_handler(s_portal, &redirect_connect); httpd_register_uri_handler(s_portal, &redirect_ncsi);
    if (httpd_register_uri_handler(s_portal, &reset_uri) != ESP_OK)
        ESP_LOGE(TAG, "Failed to register reset endpoint");
    dns_start();
    return ESP_OK;
}

void setup_portal_stop(void) {
    if (s_portal != NULL) { httpd_stop(s_portal); s_portal = NULL; }
    dns_stop();
}
