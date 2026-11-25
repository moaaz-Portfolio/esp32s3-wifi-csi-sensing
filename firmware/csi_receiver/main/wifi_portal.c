#include "wifi_portal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "WIFI_PORTAL";
static const char *NVS_NAMESPACE = "wifi_cfg";
static const char *AP_SSID = "ESP32-CSI-SETUP";
static char s_ap_password[13];

static esp_netif_t *s_ap_netif;
static httpd_handle_t s_server;
static volatile bool s_portal_active;
static volatile bool s_connecting;
static volatile bool s_connected;
static char s_pending_ssid[33];
static char s_pending_password[65];
static char s_last_error[96];

static const char INDEX_HTML[] = "<!doctype html>\n<html lang=\"ar\" dir=\"rtl\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n<title>إعداد Wi-Fi | ESP32 CSI</title>\n<style>\n:root{color-scheme:dark;--bg:#080b14;--card:#111827;--line:#263247;--accent:#8b5cf6;--txt:#eef2ff;--muted:#9aa8bd}\n*{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at top,#202044 0,#080b14 48%);color:var(--txt);font-family:Tahoma,Arial,sans-serif;display:grid;place-items:center;padding:22px}\nmain{width:min(100%,520px);background:rgba(17,24,39,.96);border:1px solid var(--line);border-radius:22px;padding:25px;box-shadow:0 24px 80px #0008}\n.logo{width:48px;height:48px;border-radius:15px;background:#8b5cf6;display:grid;place-items:center;font-size:24px;margin-bottom:16px}\nh1{font-size:23px;margin:0 0 8px}p{color:var(--muted);line-height:1.7;margin:0 0 20px}\nlabel{display:block;font-size:13px;color:#cbd5e1;margin:16px 0 8px}\nselect,input{width:100%;border:1px solid var(--line);border-radius:12px;background:#0b1220;color:var(--txt);padding:14px;font-size:15px;outline:none}\nselect:focus,input:focus{border-color:var(--accent)}\n.row{display:flex;gap:9px}.row>*{flex:1}\nbutton{border:0;border-radius:12px;padding:13px 16px;background:var(--accent);color:white;font-weight:700;font-size:14px;cursor:pointer}\nbutton.secondary{background:#263247}button:disabled{opacity:.5;cursor:wait}\n.actions{display:flex;gap:10px;margin-top:17px}.actions button{flex:1}\n#status{margin-top:17px;padding:12px 14px;background:#0b1220;border:1px solid var(--line);border-radius:12px;color:#cbd5e1;line-height:1.6;min-height:48px}\n.small{font-size:12px;color:var(--muted);margin-top:14px}\n</style>\n</head>\n<body>\n<main>\n<div class=\"logo\">⌁</div>\n<h1>إعداد اتصال Wi-Fi</h1>\n<p>اختار شبكة 2.4GHz القريبة من الجهاز، واكتب كلمة المرور. الجهاز هيحفظها ويتصل تلقائيًا في المرات الجاية.</p>\n<div class=\"row\">\n<div style=\"flex:1\">\n<label for=\"networks\">الشبكات المتاحة</label>\n<select id=\"networks\"><option value=\"\">اضغط بحث عن الشبكات</option></select>\n</div>\n<div style=\"flex:0 0 112px;align-self:end\">\n<button id=\"scan\" class=\"secondary\" style=\"width:100%\">بحث</button>\n</div>\n</div>\n<label for=\"ssid\">اسم الشبكة (SSID)</label>\n<input id=\"ssid\" maxlength=\"32\" autocomplete=\"off\" placeholder=\"اسم الشبكة أو اختارها من القائمة\">\n<label for=\"password\">كلمة المرور</label>\n<input id=\"password\" type=\"password\" maxlength=\"64\" autocomplete=\"new-password\" placeholder=\"كلمة مرور الشبكة\">\n<div class=\"actions\">\n<button id=\"connect\">حفظ واتصال</button>\n<button id=\"toggle\" class=\"secondary\">إظهار</button>\n</div>\n<div id=\"status\">الجهاز جاهز للإعداد على العنوان 192.168.4.1</div>\n<div class=\"small\">شبكة الإعداد المؤقتة هتقفل بعد نجاح الاتصال. ما تقفلش الصفحة أثناء محاولة الاتصال.</div>\n</main>\n<script>\nconst $=id=>document.getElementById(id);\nconst statusBox=$('status');\nfunction say(t){statusBox.textContent=t}\n$('networks').addEventListener('change',()=>{$('ssid').value=$('networks').value});\n$('scan').addEventListener('click',async()=>{\n  $('scan').disabled=true;say('جاري البحث عن الشبكات...');\n  try{\n    const r=await fetch('/scan');const d=await r.json();\n    if(!r.ok)throw new Error(d.error||'تعذر البحث');\n    $('networks').innerHTML='<option value=\"\">اختار شبكة</option>';\n    d.networks.forEach(n=>{\n      const o=document.createElement('option');o.value=n.ssid;\n      o.textContent=n.ssid+' · '+n.rssi+' dBm'+(n.secure?' · محمية':' · مفتوحة');\n      $('networks').appendChild(o);\n    });\n    say('تم العثور على '+d.networks.length+' شبكة. اختار شبكتك.');\n  }catch(e){say('مشكلة في البحث: '+e.message)}\n  finally{$('scan').disabled=false}\n});\n$('toggle').addEventListener('click',()=>{\n const p=$('password');p.type=p.type==='password'?'text':'password';\n $('toggle').textContent=p.type==='password'?'إظهار':'إخفاء';\n});\n$('connect').addEventListener('click',async()=>{\n const ssid=$('ssid').value.trim(),password=$('password').value;\n if(!ssid){say('اكتب اسم الشبكة أو اختارها الأول.');return}\n if(password.length>0&&password.length<8){say('كلمة مرور Wi-Fi لازم تكون 8 أحرف على الأقل، أو اتركها فاضية للشبكة المفتوحة.');return}\n $('connect').disabled=true;$('scan').disabled=true;say('جاري الاتصال بالشبكة...');\n try{\n  const r=await fetch('/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,password})});\n  const d=await r.json();if(!r.ok)throw new Error(d.error||'تعذر بدء الاتصال');\n  say('طلب الاتصال اتبعت. بنختبر كلمة المرور والاتصال...');\n }catch(e){say('فشل الطلب: '+e.message);$('connect').disabled=false;$('scan').disabled=false}\n});\nasync function poll(){\n try{\n  const d=await (await fetch('/status')).json();\n  if(d.connected){say('تم الاتصال بنجاح! الجهاز حفظ الشبكة. شبكة الإعداد هتقفل دلوقتي.')}\n  else if(d.connecting){say('جاري الاتصال بالشبكة '+(d.ssid||'')+'...')}\n  else if(d.error){say('فشل الاتصال: '+d.error+'. راجع كلمة المرور وجرب تاني.');$('connect').disabled=false;$('scan').disabled=false}\n }catch(e){}\n}\nsetInterval(poll,1200);\n</script>\n</body></html>";

static void send_json_error(httpd_req_t *req, const char *message, const char *status)
{
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "error", message);
        char *body = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (body) {
            httpd_resp_set_status(req, status);
            httpd_resp_set_type(req, "application/json; charset=utf-8");
            httpd_resp_sendstr(req, body);
            free(body);
            return;
        }
    }
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    if (!s_portal_active) {
        send_json_error(req, "Setup portal is not active", "409 Conflict");
        return ESP_OK;
    }
    if (s_connecting) {
        send_json_error(req, "Wait for the connection attempt to finish", "409 Conflict");
        return ESP_OK;
    }

    /* Pause promiscuous capture briefly so the Wi-Fi driver can perform a scan. */
    (void)esp_wifi_set_promiscuous(false);

    wifi_scan_config_t scan_config = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .show_hidden = false,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        (void)esp_wifi_set_promiscuous(true);
        send_json_error(req, "Wi-Fi scan failed; try again", "503 Service Unavailable");
        return ESP_OK;
    }

    uint16_t total = 0;
    err = esp_wifi_scan_get_ap_num(&total);
    if (err != ESP_OK) {
        (void)esp_wifi_set_promiscuous(true);
        send_json_error(req, "Could not read scan results", "500 Internal Server Error");
        return ESP_OK;
    }

    const uint16_t limit = 25;
    uint16_t count = total > limit ? limit : total;
    wifi_ap_record_t *records = count ? calloc(count, sizeof(wifi_ap_record_t)) : NULL;
    if (count && !records) {
        (void)esp_wifi_clear_ap_list();
        (void)esp_wifi_set_promiscuous(true);
        send_json_error(req, "Not enough memory for scan results", "503 Service Unavailable");
        return ESP_OK;
    }

    if (count) {
        err = esp_wifi_scan_get_ap_records(&count, records);
        if (err != ESP_OK) {
            free(records);
            (void)esp_wifi_clear_ap_list();
            (void)esp_wifi_set_promiscuous(true);
            send_json_error(req, "Could not read access point list", "500 Internal Server Error");
            return ESP_OK;
        }
    } else {
        (void)esp_wifi_clear_ap_list();
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();
    if (!root || !networks) {
        if (root) cJSON_Delete(root);
        if (networks) cJSON_Delete(networks);
        free(records);
        (void)esp_wifi_set_promiscuous(true);
        send_json_error(req, "JSON allocation failed", "500 Internal Server Error");
        return ESP_OK;
    }

    for (uint16_t i = 0; i < count; ++i) {
        char ssid[33] = {0};
        memcpy(ssid, records[i].ssid, sizeof(records[i].ssid));
        if (ssid[0] == '\0') continue;

        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddStringToObject(item, "ssid", ssid);
        cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(item, "channel", records[i].primary);
        cJSON_AddBoolToObject(item, "secure", records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(networks, item);
    }

    cJSON_AddItemToObject(root, "networks", networks);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(records);
    (void)esp_wifi_set_promiscuous(true);

    if (!body) {
        send_json_error(req, "JSON allocation failed", "500 Internal Server Error");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_sendstr(req, body);
    free(body);
    return ESP_OK;
}

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 256) {
        send_json_error(req, "Invalid request size", "400 Bad Request");
        return ESP_OK;
    }

    char *body = calloc(1, (size_t)req->content_len + 1);
    if (!body) {
        send_json_error(req, "Not enough memory", "503 Service Unavailable");
        return ESP_OK;
    }

    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) {
            free(body);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Could not read request body");
            return ESP_OK;
        }
        received += n;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    cJSON *ssid_item = root ? cJSON_GetObjectItemCaseSensitive(root, "ssid") : NULL;
    cJSON *password_item = root ? cJSON_GetObjectItemCaseSensitive(root, "password") : NULL;
    if (!cJSON_IsString(ssid_item) || !cJSON_IsString(password_item)) {
        if (root) cJSON_Delete(root);
        send_json_error(req, "SSID and password are required", "400 Bad Request");
        return ESP_OK;
    }

    const char *ssid = ssid_item->valuestring;
    const char *password = password_item->valuestring;
    size_t ssid_len = strlen(ssid);
    size_t password_len = strlen(password);
    if (ssid_len == 0 || ssid_len > 32 || password_len > 64 ||
        (password_len > 0 && password_len < 8)) {
        cJSON_Delete(root);
        send_json_error(req, "Check the network name and password length", "400 Bad Request");
        return ESP_OK;
    }
    if (!s_portal_active) {
        cJSON_Delete(root);
        send_json_error(req, "Setup portal is not active", "409 Conflict");
        return ESP_OK;
    }

    memset(s_pending_ssid, 0, sizeof(s_pending_ssid));
    memset(s_pending_password, 0, sizeof(s_pending_password));
    memcpy(s_pending_ssid, ssid, ssid_len);
    memcpy(s_pending_password, password, password_len);
    s_last_error[0] = '\0';

    wifi_config_t sta_config = {0};
    memcpy(sta_config.sta.ssid, s_pending_ssid, ssid_len);
    memcpy(sta_config.sta.password, s_pending_password, password_len);
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;

    /* Disconnect the previous attempt before applying the new network. */
    (void)esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err == ESP_OK) {
        s_connected = false;
        s_connecting = true;
        err = esp_wifi_connect();
    }
    cJSON_Delete(root);

    if (err != ESP_OK) {
        s_connecting = false;
        snprintf(s_last_error, sizeof(s_last_error), "Wi-Fi error (%s)", esp_err_to_name(err));
        send_json_error(req, "ESP32 could not start the connection; try again", "500 Internal Server Error");
        return ESP_OK;
    }

    cJSON *response = cJSON_CreateObject();
    if (response) {
        cJSON_AddBoolToObject(response, "accepted", true);
        char *json = cJSON_PrintUnformatted(response);
        cJSON_Delete(response);
        if (json) {
            httpd_resp_set_type(req, "application/json; charset=utf-8");
            httpd_resp_sendstr(req, json);
            free(json);
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not create response");
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
        return ESP_OK;
    }
    cJSON_AddBoolToObject(root, "connected", s_connected);
    cJSON_AddBoolToObject(root, "connecting", s_connecting);
    if (s_pending_ssid[0]) cJSON_AddStringToObject(root, "ssid", s_pending_ssid);
    if (s_last_error[0]) cJSON_AddStringToObject(root, "error", s_last_error);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_sendstr(req, body);
    free(body);
    return ESP_OK;
}

static void save_pending_credentials(void)
{
    if (!s_pending_ssid[0]) return;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not open credential storage: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_str(handle, "ssid", s_pending_ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "password", s_pending_password);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi credentials saved securely in device NVS");
    } else {
        ESP_LOGE(TAG, "Could not save Wi-Fi credentials: %s", esp_err_to_name(err));
    }
}

static void close_portal_task(void *arg)
{
    /* Give the browser time to receive the success status before AP shutdown. */
    vTaskDelay(pdMS_TO_TICKS(7000));

    (void)esp_wifi_set_mode(WIFI_MODE_STA);
    if (s_server) {
        httpd_handle_t server = s_server;
        s_server = NULL;
        (void)httpd_stop(server);
    }
    s_portal_active = false;
    ESP_LOGI(TAG, "Setup AP closed; station mode remains active");
    vTaskDelete(NULL);
}

esp_err_t wifi_portal_load_credentials(char *ssid, size_t ssid_size,
                                       char *password, size_t password_size)
{
    if (!ssid || !password || ssid_size < 33 || password_size < 65) {
        return ESP_ERR_INVALID_ARG;
    }
    ssid[0] = '\0';
    password[0] = '\0';

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t ssid_len = ssid_size;
    size_t password_len = password_size;
    err = nvs_get_str(handle, "ssid", ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, "password", password, &password_len);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ssid[0] = '\0';
        password[0] = '\0';
    }
    return err;
}

esp_err_t wifi_portal_prepare(void)
{
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (!s_ap_netif) return ESP_FAIL;
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) return err;

    uint8_t mac[6] = {0};
    err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) return err;

    /* A per-device key prevents nearby strangers from joining the setup AP. */
    snprintf(s_ap_password, sizeof(s_ap_password), "CSI-%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    wifi_config_t ap_config = {0};
    memcpy(ap_config.ap.ssid, AP_SSID, strlen(AP_SSID));
    ap_config.ap.ssid_len = strlen(AP_SSID);
    memcpy(ap_config.ap.password, s_ap_password, strlen(s_ap_password));
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err == ESP_OK) {
        s_portal_active = true;
        ESP_LOGW(TAG, "Setup Wi-Fi is active: %s", AP_SSID);
        ESP_LOGW(TAG, "Temporary setup password: %s", s_ap_password);
        ESP_LOGW(TAG, "Connect to it, then open http://192.168.4.1");
    }
    return err;
}

esp_err_t wifi_portal_start_server(void)
{
    if (!s_portal_active || s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.stack_size = 8192;
    config.recv_wait_timeout = 15;
    config.send_wait_timeout = 5;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start setup web server: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
    const httpd_uri_t scan_uri = {.uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler};
    const httpd_uri_t connect_uri = {.uri = "/connect", .method = HTTP_POST, .handler = connect_post_handler};
    const httpd_uri_t status_uri = {.uri = "/status", .method = HTTP_GET, .handler = status_get_handler};

    if ((err = httpd_register_uri_handler(s_server, &root_uri)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &scan_uri)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &connect_uri)) != ESP_OK ||
        (err = httpd_register_uri_handler(s_server, &status_uri)) != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGE(TAG, "Could not register setup routes: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Wi-Fi setup page ready at http://192.168.4.1");
    return ESP_OK;
}

bool wifi_portal_is_active(void)
{
    return s_portal_active;
}

void wifi_portal_on_disconnected(void)
{
    s_connected = false;
    if (s_connecting) {
        s_connecting = false;
        snprintf(s_last_error, sizeof(s_last_error), "Connection failed. Check the password and retry.");
        ESP_LOGW(TAG, "Wi-Fi connection attempt failed; setup portal remains available");
    }
}

void wifi_portal_on_connected(void)
{
    s_connected = true;
    s_connecting = false;
    s_last_error[0] = '\0';

    if (s_portal_active) {
        save_pending_credentials();
        if (xTaskCreate(close_portal_task, "portal_close", 3072, NULL, 4, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Could not create portal shutdown task; setup AP will remain active");
        }
    }
}
