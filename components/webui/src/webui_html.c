/*
 * Copyright (c) 2025, Adam G. Sweeney <agsweeney@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "webui.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "webui_html";

#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
#define MOTOMAN_NAV_ROW "<div style=\"margin-top:8px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px\">" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-status\">Motoman Status</a>" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-job\">Motoman Job</a>" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-robot-position\">Motoman Position</a>" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-position-deviation\">Motoman Deviation</a>" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-torque\">Motoman Torque</a>" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-io\">Motoman I/O</a>" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-register\">Motoman Register</a>" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-variable-b\">Motoman Var B</a>" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-variable-i\">Motoman Var I</a>" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-variable-d\">Motoman Var D</a>" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-variable-r\">Motoman Var R</a>" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-variable-s\">Motoman Var S</a>" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-position\">Motoman Var P</a>" \
"<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-alarms\">Motoman Alarms</a>" \
"</div>"
#endif

#define IP_PERSIST_SCRIPT "function setupIpPersistence(){var stored=localStorage.getItem('enipScannerIp')||'';var inputs=document.querySelectorAll('input[type=\"text\"]');for(var i=0;i<inputs.length;i++){var el=inputs[i];var id=(el.id||'').toLowerCase();if(id==='ip'||id==='gw'||id==='dns1'||id==='dns2'||id==='nm'){continue;}var ph=(el.getAttribute('placeholder')||'').toLowerCase();var looksIp=(id.indexOf('ip')>=0)||(ph.indexOf('192.')===0)||(ph.indexOf('ip')>=0);if(looksIp){if(!el.value&&stored){el.value=stored;}el.addEventListener('input',function(e){var v=e.target.value.trim();if(v){localStorage.setItem('enipScannerIp',v);}});}}}document.addEventListener('DOMContentLoaded',setupIpPersistence);"

// Main page HTML
static const char index_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>EtherNet/IP Scanner</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
".n span.active{background:#9e9e9e;opacity:0.6;display:block;margin:0;padding:8px 15px;color:#fff;border-radius:4px;text-align:center}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input,select,textarea{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px;margin-right:10px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
"textarea{font-family:monospace}"
".hex-grid{display:grid;grid-template-columns:repeat(16,1fr);gap:2px;margin:10px 0}"
".hex-cell{background:#f0f0f0;border:1px solid #ddd;padding:4px 2px;text-align:center;font-family:monospace;font-size:12px;cursor:pointer;min-width:45px}"
".hex-cell:hover{background:#e0e0e0}"
".hex-cell input{width:100%;min-width:40px;border:none;background:transparent;text-align:center;font-family:monospace;font-size:12px;padding:2px}"
".hex-cell input:focus{background:#fff;outline:2px solid #4CAF50;width:100%}"
".hex-offset{font-family:monospace;font-size:11px;color:#666;text-align:right;padding-right:5px;min-width:50px}"
".hex-header{display:grid;grid-template-columns:60px repeat(8,minmax(45px,1fr));gap:2px;margin-bottom:5px}"
".hex-header-cell{text-align:center;font-size:10px;color:#666;font-weight:bold;min-width:45px}"
".hex-row{display:grid;grid-template-columns:60px repeat(8,minmax(45px,1fr));gap:2px;margin-bottom:2px}"
"</style></head><body>"
"<div class=\"c\"><h1>EtherNet/IP Scanner</h1>"
"<div class=\"n\" id=\"mainNav\"></div>"
"<label for=\"writeIpAddress\">IP Address:</label>"
"<div style=\"display: flex; gap: 10px; align-items: center; margin-bottom: 10px;\">"
"<select id=\"writeIpAddressSelect\" onchange=\"updateIpAddress()\" style=\"flex: 1; max-width: 350px; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; display: none;\">"
"<option value=\"\">Select a device...</option>"
"</select>"
"<input type=\"text\" id=\"writeIpAddress\" placeholder=\"192.168.1.100\" value=\"\" style=\"flex: 1; max-width: 350px; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; display: block;\">"
"<button onclick=\"scanDevices()\" style=\"white-space: nowrap; padding: 8px 15px; flex-shrink: 0;\">Discover Devices</button>"
"</div>"
"<small style=\"color: #666; margin-top: -5px; margin-bottom: 15px; display: block;\">Click Discover Devices to scan the network, or enter IP address manually</small>"
"<label for=\"writeAssemblyInstance\">Assembly Instance:</label>"
"<div style=\"display: flex; gap: 10px; align-items: center; margin-bottom: 5px;\">"
"<select id=\"writeAssemblyInstanceSelect\" onchange=\"updateAssemblyInstance()\" style=\"flex: 0 0 200px; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; display: none;\">"
"<option value=\"\">Select an instance...</option>"
"</select>"
"<input type=\"number\" id=\"writeAssemblyInstance\" placeholder=\"Enter assembly instance number\" value=\"\" min=\"1\" max=\"65535\" style=\"flex: 0 0 200px; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box;\">"
"<button onclick=\"discoverAssemblies()\" style=\"white-space: nowrap; padding: 8px 15px; flex-shrink: 0;\">Discover</button>"
"</div>"
"<small style=\"color: #666; margin-top: -5px; margin-bottom: 15px; display: block;\">Click Discover to auto-detect instances, or enter manually</small>"
"<label for=\"writeTimeout\">Timeout (ms):</label>"
"<input type=\"number\" id=\"writeTimeout\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\" style=\"max-width: 150px; width: 100%; padding: 8px; margin-bottom: 10px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box;\">"
"<button onclick=\"readAssemblyForWrite()\" style=\"margin-bottom: 15px;\">Read Assembly</button>"
"<div id=\"byteEditContainer\"><label>Data (Decimal Editor - Click to edit, values 0-255):</label>"
"<div id=\"hexGrid\" style=\"background:#fff;padding:10px;border:1px solid #ddd;border-radius:4px;max-height:400px;overflow-y:auto\"></div></div>"
"<button id=\"writeButton\" onclick=\"writeAssembly()\" style=\"margin-top:10px;width:auto;min-width:150px\">Write Assembly</button>"
"<div id=\"writeResults\"></div></div>"
"<script>"
"document.addEventListener('DOMContentLoaded', function() {"
"  const nav = document.getElementById('mainNav');"
"  if (nav) {"
"    let navHtml = '<span class=\"active\">Assembly I/O</span>';"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"    navHtml += '<a href=\"/tags\">Read Tag</a>';"
"    navHtml += '<a href=\"/write-tag\">Write Tag</a>';"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"    navHtml += '<a href=\"/implicit\">Implicit I/O</a>';"
#endif
"    navHtml += '<a href=\"/network\">Network</a>';"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
"    navHtml += '<div style=\"margin-top:8px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px\">';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-status\">Motoman Status</a>';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-job\">Motoman Job</a>';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-robot-position\">Motoman Position</a>';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-position-deviation\">Motoman Deviation</a>';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-torque\">Motoman Torque</a>';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-io\">Motoman I/O</a>';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-register\">Motoman Register</a>';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-variable-b\">Motoman Var B</a>';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-variable-i\">Motoman Var I</a>';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-variable-d\">Motoman Var D</a>';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-variable-r\">Motoman Var R</a>';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-variable-s\">Motoman Var S</a>';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-position\">Motoman Var P</a>';"
"    navHtml += '<a style=\"display:block;text-align:center;margin:0\" href=\"/motoman-alarms\">Motoman Alarms</a>';"
"    navHtml += '</div>';"
#endif
"    nav.innerHTML = navHtml;"
"  }"
"});"
"function updateIpAddress() {"
"  const select = document.getElementById('writeIpAddressSelect');"
"  const input = document.getElementById('writeIpAddress');"
"  if (select.value) {"
"    input.value = select.value;"
"  }"
"}"
"function scanDevices() {"
"  const select = document.getElementById('writeIpAddressSelect');"
"  const input = document.getElementById('writeIpAddress');"
"  const resultsDiv = document.getElementById('writeResults');"
"  resultsDiv.innerHTML = '<p>Scanning for devices...</p>';"
"  fetch('/api/scanner/scan')"
"    .then(response => response.json())"
"    .then(data => {"
"      if (data.status === 'ok') {"
"        if (data.count === 0) {"
"          resultsDiv.innerHTML = '<div class=\"e\">No devices found</div>';"
"          select.style.display = 'none';"
"          input.style.display = 'block';"
"        } else {"
"          select.innerHTML = '<option value=\"\">Select a device...</option>';"
"          data.devices.forEach(device => {"
"            const option = document.createElement('option');"
"            option.value = device.ip_address;"
"            option.textContent = device.ip_address + ' - ' + (device.product_name || 'Unknown');"
"            select.appendChild(option);"
"          });"
"          select.style.display = 'block';"
"          input.style.display = 'none';"
"          resultsDiv.innerHTML = '<div class=\"s\">Found ' + data.count + ' device(s). Select from dropdown.</div>';"
"        }"
"      } else {"
"        resultsDiv.innerHTML = '<div class=\"e\">Scan failed</div>';"
"      }"
"    })"
"    .catch(error => {"
"      resultsDiv.innerHTML = '<div class=\"e\">Error: ' + error.message + '</div>';"
"    });"
"}"
"function updateAssemblyInstance() {"
"  const select = document.getElementById('writeAssemblyInstanceSelect');"
"  const input = document.getElementById('writeAssemblyInstance');"
"  if (select.value) {"
"    input.value = select.value;"
"  }"
"}"
"function discoverAssemblies() {"
"  const ipAddress = document.getElementById('writeIpAddress').value;"
"  const timeout = parseInt(document.getElementById('writeTimeout').value);"
"  const select = document.getElementById('writeAssemblyInstanceSelect');"
"  const input = document.getElementById('writeAssemblyInstance');"
"  const resultsDiv = document.getElementById('writeResults');"
"  if (!ipAddress) {"
"    resultsDiv.innerHTML = '<div class=\"e\">Please enter an IP address first</div>';"
"    return;"
"  }"
"  resultsDiv.innerHTML = '<p>Discovering assembly instances...</p>';"
"  fetch('/api/scanner/discover-assemblies', {"
"    method: 'POST',"
"    headers: { 'Content-Type': 'application/json' },"
"    body: JSON.stringify({"
"      ip_address: ipAddress,"
"      timeout_ms: timeout"
"    })"
"  })"
"    .then(response => response.json())"
"    .then(data => {"
"      if (data.status === 'ok' && data.count > 0) {"
"        select.innerHTML = '<option value=\"\">Select an instance...</option>';"
"        data.instances.forEach(instance => {"
"          const option = document.createElement('option');"
"          option.value = instance;"
"          option.textContent = 'Instance ' + instance;"
"          select.appendChild(option);"
"        });"
"        select.style.display = 'block';"
"        input.style.display = 'none';"
"        resultsDiv.innerHTML = '<div class=\"s\">Found ' + data.count + ' assembly instance(s). Select from dropdown.</div>';"
"      } else {"
"        select.style.display = 'none';"
"        input.style.display = 'block';"
"        resultsDiv.innerHTML = '<div class=\"e\">No assembly instances found. Please enter manually.</div>';"
"      }"
"    })"
"    .catch(error => {"
"      select.style.display = 'none';"
"      input.style.display = 'block';"
"      resultsDiv.innerHTML = '<div class=\"e\">Discovery failed: ' + error.message + '</div>';"
"    });"
"}"
"function readAssemblyForWrite() {"
"  const ipAddress = document.getElementById('writeIpAddress').value;"
"  const assemblyInstance = parseInt(document.getElementById('writeAssemblyInstance').value);"
"  const timeout = parseInt(document.getElementById('writeTimeout').value);"
"  const resultsDiv = document.getElementById('writeResults');"
"  if (!ipAddress) {"
"    resultsDiv.innerHTML = '<div class=\"e\">Please enter an IP address</div>';"
"    return;"
"  }"
"  if (!assemblyInstance || assemblyInstance < 1) {"
"    resultsDiv.innerHTML = '<div class=\"e\">Please enter a valid assembly instance</div>';"
"    return;"
"  }"
"  resultsDiv.innerHTML = '<p>Reading assembly data...</p>';"
"  fetch('/api/scanner/read-assembly', {"
"    method: 'POST',"
"    headers: { 'Content-Type': 'application/json' },"
"    body: JSON.stringify({"
"      ip_address: ipAddress,"
"      assembly_instance: assemblyInstance,"
"      timeout_ms: timeout"
"    })"
"  })"
"    .then(response => response.json())"
"    .then(data => {"
"      if (data.success) {"
"        resultsDiv.innerHTML = '<div class=\"s\">Assembly read successfully! Data loaded into form.</div>';"
"        populateWriteForm(data.data);"
"        checkWritable(ipAddress, assemblyInstance, timeout);"
"      } else {"
"        resultsDiv.innerHTML = '<div class=\"e\">Error: ' + (data.error || 'Unknown error') + '</div>';"
"      }"
"    })"
"    .catch(error => {"
"      resultsDiv.innerHTML = '<div class=\"e\">Error: ' + error.message + '</div>';"
"    });"
"}"
"function checkWritable(ipAddress, assemblyInstance, timeout) {"
"  fetch('/api/scanner/check-writable', {"
"    method: 'POST',"
"    headers: { 'Content-Type': 'application/json' },"
"    body: JSON.stringify({"
"      ip_address: ipAddress,"
"      assembly_instance: assemblyInstance,"
"      timeout_ms: timeout"
"    })"
"  })"
"    .then(response => response.json())"
"    .then(data => {"
"      const writeButton = document.getElementById('writeButton');"
"      if (data.writable) {"
"        writeButton.style.display = 'inline-block';"
"      } else {"
"        writeButton.style.display = 'none';"
"        const resultsDiv = document.getElementById('writeResults');"
"        resultsDiv.innerHTML = '<div class=\"e\">This assembly is not writable</div>';"
"      }"
"    })"
"    .catch(error => {"
"      console.error('Error checking writability:', error);"
"      document.getElementById('writeButton').style.display = 'inline-block';"
"    });"
"}"
"function populateWriteForm(data) {"
"  if (!data || data.length === 0) return;"
"  updateHexGrid(data);"
"}"
"function updateHexGrid(bytes) {"
"  const container = document.getElementById('hexGrid');"
"  container.innerHTML = '';"
"  if (!bytes || bytes.length === 0) return;"
"  container.dataset.originalLength = bytes.length.toString();"
"  const headerRow = document.createElement('div');"
"  headerRow.className = 'hex-header';"
"  headerRow.innerHTML = '<div class=\"hex-header-cell\">Offset</div>';"
"  for (let i = 0; i < 8; i++) {"
"    const headerCell = document.createElement('div');"
"    headerCell.className = 'hex-header-cell';"
"    headerCell.textContent = i.toString().padStart(3, '0');"
"    headerRow.appendChild(headerCell);"
"  }"
"  container.appendChild(headerRow);"
"  for (let row = 0; row < Math.ceil(bytes.length / 8); row++) {"
"    const rowDiv = document.createElement('div');"
"    rowDiv.className = 'hex-row';"
"    const offsetCell = document.createElement('div');"
"    offsetCell.className = 'hex-offset';"
"    offsetCell.textContent = (row * 8).toString().padStart(4, '0');"
"    rowDiv.appendChild(offsetCell);"
"    for (let col = 0; col < 8; col++) {"
"      const index = row * 8 + col;"
"      const cell = document.createElement('div');"
"      cell.className = 'hex-cell';"
"      const input = document.createElement('input');"
"      input.type = 'text';"
"      input.maxLength = 3;"
"      if (index < bytes.length) {"
"        input.value = bytes[index].toString();"
"      } else {"
"        input.disabled = true;"
"        input.style.background = '#f5f5f5';"
"        input.value = '';"
"      }"
"      input.dataset.index = index;"
"      input.oninput = function(e) {"
"        let val = this.value.replace(/[^0-9]/g, '');"
"        if (val.length > 3) {"
"          val = val.substring(0, 3);"
"        }"
"        this.value = val;"
"      };"
"      input.onblur = function() {"
"        if (this.value.length === 0) {"
"          this.value = '0';"
"        } else {"
"          const val = parseInt(this.value, 10);"
"          if (isNaN(val) || val < 0 || val > 255) {"
"            this.value = '0';"
"          } else {"
"            this.value = val.toString();"
"          }"
"        }"
"      };"
"      cell.appendChild(input);"
"      rowDiv.appendChild(cell);"
"    }"
"    container.appendChild(rowDiv);"
"  }"
"}"
"function getBytesFromHexGrid() {"
"  const container = document.getElementById('hexGrid');"
"  const originalLength = parseInt(container.dataset.originalLength || '0');"
"  if (originalLength === 0) return [];"
"  const inputs = document.querySelectorAll('#hexGrid input:not([disabled])');"
"  const bytes = [];"
"  for (let i = 0; i < originalLength && i < inputs.length; i++) {"
"    const val = parseInt(inputs[i].value, 10);"
"    if (!isNaN(val) && val >= 0 && val <= 255) {"
"      bytes.push(val);"
"    } else {"
"      bytes.push(0);"
"    }"
"  }"
"  return bytes;"
"}"
"function writeAssembly() {"
"  const ipAddress = document.getElementById('writeIpAddress').value;"
"  const assemblyInstance = parseInt(document.getElementById('writeAssemblyInstance').value);"
"  const timeout = parseInt(document.getElementById('writeTimeout').value);"
"  const resultsDiv = document.getElementById('writeResults');"
"  if (!ipAddress) {"
"    resultsDiv.innerHTML = '<div class=\"e\">Please enter an IP address</div>';"
"    return;"
"  }"
"  if (!assemblyInstance || assemblyInstance < 1) {"
"    resultsDiv.innerHTML = '<div class=\"e\">Please enter a valid assembly instance</div>';"
"    return;"
"  }"
"  let data = getBytesFromHexGrid();"
"  if (data.length === 0) {"
"    resultsDiv.innerHTML = '<div class=\"e\">Please read assembly data first or enter data in editor</div>';"
"    return;"
"  }"
"  if (data.length === 0) {"
"    resultsDiv.innerHTML = '<div class=\"e\">No data to write</div>';"
"    return;"
"  }"
"  resultsDiv.innerHTML = '<p>Writing assembly data...</p>';"
"  fetch('/api/scanner/write-assembly', {"
"    method: 'POST',"
"    headers: { 'Content-Type': 'application/json' },"
"    body: JSON.stringify({"
"      ip_address: ipAddress,"
"      assembly_instance: assemblyInstance,"
"      timeout_ms: timeout,"
"      data: data"
"    })"
"  })"
"    .then(response => response.json())"
"    .then(data => {"
"      if (data.success) {"
"        resultsDiv.innerHTML = '<div class=\"s\">Assembly written successfully!</div>';"
"      } else {"
"        resultsDiv.innerHTML = '<div class=\"e\">Error: ' + (data.error || 'Unknown error') + '</div>';"
"      }"
"    })"
"    .catch(error => {"
"      resultsDiv.innerHTML = '<div class=\"e\">Error: ' + error.message + '</div>';"
"    });"
"}"
"</script>"
"</body>"
"</html>";

// GET / - Serve Read/Write Assembly page
static esp_err_t webui_index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t html_len = strlen(index_page);
    ESP_LOGD(TAG, "Sending read/write page, length: %zu bytes", html_len);
    
    // Send in chunks to handle large HTML content
    const size_t chunk_size = 4096;  // Send 4KB chunks
    size_t sent = 0;
    esp_err_t ret = ESP_OK;
    
    while (sent < html_len && ret == ESP_OK) {
        size_t to_send = (html_len - sent < chunk_size) ? (html_len - sent) : chunk_size;
        ret = httpd_resp_send_chunk(req, index_page + sent, to_send);
        if (ret == ESP_OK) {
            sent += to_send;
        }
    }
    
    if (ret == ESP_OK) {
        // Send final empty chunk to indicate end of response
        ret = httpd_resp_send_chunk(req, NULL, 0);
    }
    
    return ret;
}

#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
// Tag test page HTML
static const char tags_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Read Tag</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
".n span.active{background:#9e9e9e;opacity:0.6;display:block;margin:0;padding:8px 15px;color:#fff;border-radius:4px;text-align:center}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input,select{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"</style></head><body>"
"<div class=\"c\"><h1>Read Tag</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a><span class=\"active\">Read Tag</span><a href=\"/write-tag\">Write Tag</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"readIpAddress\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Tag Path:</label><input type=\"text\" id=\"readTagPath\" placeholder=\"MyTag\" value=\"\">"
"<small style=\"color:#666;display:block;margin-top:-5px;margin-bottom:10px\">Examples: MyTag, MyArray[0]</small>"
"<label>Timeout (ms):</label><input type=\"number\" id=\"readTimeout\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\" style=\"max-width:150px\">"
"<button onclick=\"readTag()\">Read Tag</button><div id=\"readResults\"></div></div>"
"<script>"
"function readTag(){"
"var ip=document.getElementById('readIpAddress').value;"
"var tag=document.getElementById('readTagPath').value;"
"var to=parseInt(document.getElementById('readTimeout').value);"
"var r=document.getElementById('readResults');"
"if(!ip||!tag){r.innerHTML='<div class=\"e\">Please enter IP address and tag path</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading tag...</div>';"
"fetch('/api/scanner/read-tag',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,tag_path:tag,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">Tag read successful!</div>';"
"h+='<div style=\"margin:10px 0;padding:10px;background:#fff;border:1px solid #ddd;border-radius:4px\">';"
"h+='<div style=\"margin:5px 0;padding:5px;border-bottom:1px solid #eee\"><strong>Tag:</strong> '+d.tag_path+'</div>';"
"h+='<div style=\"margin:5px 0;padding:5px;border-bottom:1px solid #eee\"><strong>Data Type:</strong> '+d.data_type_name+' (0x'+d.cip_data_type.toString(16).toUpperCase()+')</div>';"
"h+='<div style=\"margin:5px 0;padding:5px;border-bottom:1px solid #eee\"><strong>Data Length:</strong> '+d.data_length+' bytes</div>';"
"h+='<div style=\"margin:5px 0;padding:5px;border-bottom:1px solid #eee\"><strong>Response Time:</strong> '+d.response_time_ms+' ms</div>';"
"if(d.value_string!==undefined){h+='<div style=\"margin:5px 0;padding:5px;border-bottom:1px solid #eee\"><strong>Value (STRING):</strong> '+d.value_string+'</div>';}"
"else if(d.value_bool!==undefined){h+='<div style=\"margin:5px 0;padding:5px;border-bottom:1px solid #eee\"><strong>Value (BOOL):</strong> '+d.value_bool+'</div>';}"
"else if(d.value_sint!==undefined){h+='<div style=\"margin:5px 0;padding:5px;border-bottom:1px solid #eee\"><strong>Value (SINT):</strong> '+d.value_sint+'</div>';}"
"else if(d.value_int!==undefined){h+='<div style=\"margin:5px 0;padding:5px;border-bottom:1px solid #eee\"><strong>Value (INT):</strong> '+d.value_int+'</div>';}"
"else if(d.value_dint!==undefined){h+='<div style=\"margin:5px 0;padding:5px;border-bottom:1px solid #eee\"><strong>Value (DINT):</strong> '+d.value_dint+'</div>';}"
"else if(d.value_real!==undefined){h+='<div style=\"margin:5px 0;padding:5px;border-bottom:1px solid #eee\"><strong>Value (REAL):</strong> '+d.value_real+'</div>';}"
"if(d.data_hex){h+='<div style=\"margin:5px 0;padding:5px;border-bottom:1px solid #eee\"><strong>Hex:</strong> '+d.data_hex+'</div>';}"
"if(d.data&&d.data.length>0){h+='<div style=\"margin:5px 0;padding:5px\"><strong>Raw Bytes:</strong> ['+d.data.join(', ')+']</div>';}"
"h+='</div>';"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
"window.readTag=readTag;"
IP_PERSIST_SCRIPT
"</script></body></html>";

// Write tag page HTML
static const char write_tags_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Write Tag</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
".n span.active{background:#9e9e9e;opacity:0.6;display:block;margin:0;padding:8px 15px;color:#fff;border-radius:4px;text-align:center}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input,select{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"</style></head><body>"
"<div class=\"c\"><h1>Write Tag</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a><a href=\"/tags\">Read Tag</a><span class=\"active\">Write Tag</span>"
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"ip\" placeholder=\"192.168.1.100\">"
"<label>Tag Path:</label><input type=\"text\" id=\"tag\" placeholder=\"MyTag\">"
"<label>Data Type:</label><select id=\"type\"><option value=\"193\">BOOL</option><option value=\"194\">SINT</option><option value=\"195\">INT</option><option value=\"196\" selected>DINT</option><option value=\"202\">REAL</option><option value=\"218\">STRING</option></select>"
"<label>Value:</label><input type=\"text\" id=\"val\" placeholder=\"12345\">"
"<label>Timeout (ms):</label><input type=\"number\" id=\"to\" value=\"5000\" style=\"max-width:150px\">"
"<button onclick=\"w()\">Write Tag</button><div id=\"r\"></div></div>"
"<script>"
"function w(){"
"var ip=document.getElementById('ip').value;"
"var tag=document.getElementById('tag').value;"
"var type=parseInt(document.getElementById('type').value);"
"var val=document.getElementById('val').value;"
"var to=parseInt(document.getElementById('to').value);"
"var r=document.getElementById('r');"
"if(!ip||!tag||!val){r.innerHTML='<div class=\"e\">Please enter IP, tag, and value</div>';return;}"
"var d=[];"
"try{"
"if(type==193){d=[parseInt(val)?1:0];}"
"else if(type==194){var v=parseInt(val);d=[v&0xFF];}"
"else if(type==195){var v=parseInt(val);d=[v&0xFF,(v>>8)&0xFF];}"
"else if(type==196){var v=parseInt(val);d=[v&0xFF,(v>>8)&0xFF,(v>>16)&0xFF,(v>>24)&0xFF];}"
"else if(type==202){var b=new ArrayBuffer(4);var v=new DataView(b);v.setFloat32(0,parseFloat(val),true);for(var i=0;i<4;i++)d.push(v.getUint8(i));}"
"else if(type==218){for(var i=0;i<val.length;i++){d.push(val.charCodeAt(i)&0xFF);}}"
"else{r.innerHTML='<div class=\"e\">Unsupported type</div>';return;}"
"}catch(e){r.innerHTML='<div class=\"e\">Invalid value</div>';return;}"
"r.innerHTML='<div class=\"i\">Writing...</div>';"
"fetch('/api/scanner/write-tag',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,tag_path:tag,cip_data_type:type,data:d,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(x){r.innerHTML=x.success?'<div class=\"s\">Success!</div>':'<div class=\"e\">Failed: '+(x.error||'Unknown')+'</div>';})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
"window.writeTag=w;"
IP_PERSIST_SCRIPT
"</script></body></html>";

// GET /tags - Serve Tag Test page
static esp_err_t webui_tags_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t html_len = strlen(tags_page);
    ESP_LOGD(TAG, "Sending tags page, length: %zu bytes", html_len);
    
    // Send in chunks to handle large HTML content
    const size_t chunk_size = 4096;  // Send 4KB chunks
    size_t sent = 0;
    esp_err_t ret = ESP_OK;
    
    while (sent < html_len && ret == ESP_OK) {
        size_t to_send = (html_len - sent < chunk_size) ? (html_len - sent) : chunk_size;
        ret = httpd_resp_send_chunk(req, tags_page + sent, to_send);
        if (ret == ESP_OK) {
            sent += to_send;
        }
    }
    
    if (ret == ESP_OK) {
        // Send final empty chunk to indicate end of response
        ret = httpd_resp_send_chunk(req, NULL, 0);
    }
    
    return ret;
}

// GET /write-tag - Serve Write Tag page
static esp_err_t webui_write_tags_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t html_len = strlen(write_tags_page);
    ESP_LOGD(TAG, "Sending write tags page, length: %zu bytes", html_len);
    
    // Send in chunks to handle large HTML content
    const size_t chunk_size = 4096;  // Send 4KB chunks
    size_t sent = 0;
    esp_err_t ret = ESP_OK;
    
    while (sent < html_len && ret == ESP_OK) {
        size_t to_send = (html_len - sent < chunk_size) ? (html_len - sent) : chunk_size;
        ret = httpd_resp_send_chunk(req, write_tags_page + sent, to_send);
        if (ret == ESP_OK) {
            sent += to_send;
        }
    }
    
    if (ret == ESP_OK) {
        // Send final empty chunk to indicate end of response
        ret = httpd_resp_send_chunk(req, NULL, 0);
    }
    
    return ret;
}
#endif

#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
// Implicit messaging test page HTML
static const char implicit_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Implicit I/O</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:white;padding:20px;border-radius:8px}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
"h2{color:#555;margin-top:20px}"
".fg{margin:15px 0}"
"label{display:block;margin-bottom:5px;font-weight:bold}"
"input{width:100%;padding:8px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"button{padding:10px 20px;margin:5px;border:none;border-radius:4px;cursor:pointer}"
".b1{background:#4CAF50;color:white}"
".b2{background:#f44336;color:white}"
".b3{background:#ff9800;color:white}"
".s{background:#d4edda;color:#155724;padding:10px;border-radius:4px;margin:10px 0}"
".e{background:#f8d7da;color:#721c24;padding:10px;border-radius:4px;margin:10px 0}"
".sb{background:#f8f9fa;border:1px solid #dee2e6;border-radius:4px;padding:15px;margin:10px 0}"
".si{margin:5px 0;padding:5px;background:white;border-radius:3px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n span.active{background:#9e9e9e;opacity:0.6;display:block;margin:0;padding:8px 15px;color:#fff;border-radius:4px;text-align:center}"
".hex-header{display:grid;grid-template-columns:60px repeat(8,minmax(45px,1fr));gap:2px;margin-bottom:5px}"
".hex-header-cell{text-align:center;font-size:10px;color:#666;font-weight:bold;min-width:45px}"
".hex-row{display:grid;grid-template-columns:60px repeat(8,minmax(45px,1fr));gap:2px;margin-bottom:2px}"
".hex-offset{font-family:monospace;font-size:11px;color:#666;text-align:right;padding-right:5px;min-width:50px}"
".hex-cell{background:#f0f0f0;border:1px solid #ddd;padding:4px 2px;text-align:center;font-family:monospace;font-size:12px;cursor:default;min-width:45px}"
".hex-cell:hover{background:#e0e0e0}"
"</style>"
"</head><body>"
"<div class=\"c\">"
"<h1>Implicit I/O</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
"<span class=\"active\">Implicit I/O</span>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
"<a href=\"/network\">Network</a>"
MOTOMAN_NAV_ROW
#else
"<a href=\"/network\">Network</a>"
#endif
"</div>"
"<div id=\"conn\">"
"<h2>Connection</h2>"
"<table style=\"width:100%;border-collapse:collapse;\">"
"<tr><td><label>IP:</label></td><td><input type=\"text\" id=\"ip\" value=\"192.168.1.100\" style=\"max-width:200px\"></td></tr>"
"<tr><td><label>O->T:</label></td><td><input type=\"number\" id=\"ac\" value=\"150\" min=\"1\" max=\"65535\" style=\"max-width:80px\"></td><td><label>Size:</label></td><td><input type=\"number\" id=\"asc\" value=\"0\" min=\"0\" max=\"500\" placeholder=\"0=auto\" style=\"max-width:80px\" title=\"Assembly data size in bytes (0 = autodetect)\"></td></tr>"
"<tr><td><label>T->O:</label></td><td><input type=\"number\" id=\"ap\" value=\"100\" min=\"1\" max=\"65535\" style=\"max-width:80px\"></td><td><label>Size:</label></td><td><input type=\"number\" id=\"asp\" value=\"0\" min=\"0\" max=\"500\" placeholder=\"0=auto\" style=\"max-width:80px\" title=\"Assembly data size in bytes (0 = autodetect)\"></td></tr>"
"<tr><td><label>RPI (ms):</label></td><td><input type=\"number\" id=\"rpi\" value=\"200\" min=\"10\" max=\"10000\" style=\"max-width:80px\"></td><td><label>Timeout:</label></td><td><input type=\"number\" id=\"to\" value=\"5000\" min=\"1000\" max=\"60000\" style=\"max-width:80px\"></td></tr>"
"</table>"
"<div style=\"font-size:12px;color:#666;margin-top:5px;\">Note: Size = assembly data size in bytes (0 = autodetect). Connection overhead is calculated automatically.</div>"
"<button class=\"b1\" onclick=\"oc()\">Open</button>"
"<button class=\"b2\" onclick=\"cc()\">Close</button>"
"<div id=\"cr\"></div>"
"</div>"
"<div id=\"st\" style=\"display:none;\">"
"<h2>Status</h2>"
"<div class=\"sb\" id=\"sb\"></div>"
"<h2>Write Data (O->T)</h2>"
"<div id=\"writeGrid\" style=\"background:#fff;padding:10px;border:1px solid #ddd;border-radius:4px;max-height:300px;overflow-y:auto\"></div>"
"<button class=\"b1\" onclick=\"wd()\" style=\"margin-top:10px;\">Write Data</button>"
"<h2>Received Data (T->O)</h2>"
"<div id=\"receiveGrid\" style=\"background:#fff;padding:10px;border:1px solid #ddd;border-radius:4px;max-height:300px;overflow-y:auto\"></div>"
"</div>"
"<script>"
"let si=null;"
"function initWriteGrid(s){"
"var c=document.getElementById('writeGrid');c.innerHTML='';c.dataset.size=s;"
"var h=document.createElement('div');h.className='hex-header';h.innerHTML='<div class=\"hex-header-cell\">Offset</div>';"
"for(var i=0;i<8;i++){var hc=document.createElement('div');hc.className='hex-header-cell';hc.textContent=i.toString().padStart(3,'0');h.appendChild(hc);}"
"c.appendChild(h);"
"for(var r=0;r<Math.ceil(s/8);r++){"
"var rd=document.createElement('div');rd.className='hex-row';"
"var oc=document.createElement('div');oc.className='hex-offset';oc.textContent=(r*8).toString().padStart(4,'0');rd.appendChild(oc);"
"for(var col=0;col<8;col++){"
"var idx=r*8+col;var cell=document.createElement('div');cell.className='hex-cell';"
"var inp=document.createElement('input');inp.type='text';inp.maxLength=3;"
"if(idx<s){inp.value='0';inp.dataset.index=idx;}else{inp.disabled=true;inp.style.background='#f5f5f5';}"
"inp.oninput=function(e){var v=this.value.replace(/[^0-9]/g,'');if(v.length>3)v=v.substring(0,3);this.value=v;};"
"inp.onblur=function(){if(this.value.length===0){this.value='0';}else{var v=parseInt(this.value,10);if(isNaN(v)||v<0||v>255){this.value='0';}else{this.value=v.toString();}}};"
"cell.appendChild(inp);rd.appendChild(cell);"
"}"
"c.appendChild(rd);"
"}"
"}"
"function updateReceiveGrid(bytes){"
"var c=document.getElementById('receiveGrid');c.innerHTML='';if(!bytes||bytes.length===0){c.innerHTML='<div class=\"si\">No data received</div>';return;}"
"c.dataset.size=bytes.length;"
"var h=document.createElement('div');h.className='hex-header';h.innerHTML='<div class=\"hex-header-cell\">Offset</div>';"
"for(var i=0;i<8;i++){var hc=document.createElement('div');hc.className='hex-header-cell';hc.textContent=i.toString().padStart(3,'0');h.appendChild(hc);}"
"c.appendChild(h);"
"for(var r=0;r<Math.ceil(bytes.length/8);r++){"
"var rd=document.createElement('div');rd.className='hex-row';"
"var oc=document.createElement('div');oc.className='hex-offset';oc.textContent=(r*8).toString().padStart(4,'0');rd.appendChild(oc);"
"for(var col=0;col<8;col++){"
"var idx=r*8+col;var cell=document.createElement('div');cell.className='hex-cell';"
"if(idx<bytes.length){"
"var val=bytes[idx];"
"cell.textContent=val.toString().padStart(3,'0');"
"cell.style.cursor='default';"
"}else{cell.style.background='#f5f5f5';cell.textContent='';}"
"rd.appendChild(cell);"
"}"
"c.appendChild(rd);"
"}"
"}"
"function populateWriteGrid(bytes){"
"var c=document.getElementById('writeGrid');if(!c)return;"
"var inputs=document.querySelectorAll('#writeGrid input:not([disabled])');"
"for(var i=0;i<bytes.length&&i<inputs.length;i++){inputs[i].value=bytes[i].toString();}"
"}"
"function getWriteData(){"
"var c=document.getElementById('writeGrid');var size=parseInt(c.dataset.size||'0');if(size===0)return [];"
"var inputs=document.querySelectorAll('#writeGrid input:not([disabled])');var bytes=[];"
"for(var i=0;i<size&&i<inputs.length;i++){var v=parseInt(inputs[i].value,10);bytes.push(isNaN(v)||v<0||v>255?0:v);}"
"return bytes;"
"}"
"function wd(){"
"var ip=document.getElementById('ip').value,to=parseInt(document.getElementById('to').value),r=document.getElementById('cr');"
"var data=getWriteData();if(data.length===0){r.innerHTML='<div class=\"e\">No data to write</div>';return;}"
"r.innerHTML='Writing...';"
"fetch('/api/scanner/implicit/write-data',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({ip_address:ip,data:data,timeout_ms:to})})"
".then(x=>x.json()).then(d=>{"
"if(d.success){r.innerHTML='<div class=\"s\">Written!</div>';}else{r.innerHTML='<div class=\"e\">'+d.error+'</div>';}"
"}).catch(e=>{r.innerHTML='<div class=\"e\">'+e.message+'</div>';});"
"}"
"function oc(){"
"var ip=document.getElementById('ip').value,ac=parseInt(document.getElementById('ac').value),ap=parseInt(document.getElementById('ap').value),"
"asc=parseInt(document.getElementById('asc').value)||0,asp=parseInt(document.getElementById('asp').value)||0,"
"rpi=parseInt(document.getElementById('rpi').value),to=parseInt(document.getElementById('to').value),"
"r=document.getElementById('cr');"
"if(!ip||!ac||!ap||asc<0||asp<0||asc>500||asp>500||!rpi||!to){r.innerHTML='<div class=\"e\">Invalid input (0=autodetect)</div>';return;}"
"r.innerHTML='Opening...';"
"fetch('/api/scanner/implicit/open',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({ip_address:ip,assembly_instance_consumed:ac,assembly_instance_produced:ap,"
"assembly_data_size_consumed:asc,assembly_data_size_produced:asp,rpi_ms:rpi,timeout_ms:to,exclusive_owner:true})})"
".then(x=>x.json()).then(d=>{"
"if(d.success){r.innerHTML='<div class=\"s\">Open!</div>';document.getElementById('st').style.display='block';var gridSize=d.assembly_data_size_consumed||asc||40;initWriteGrid(gridSize);"
"if(d.last_sent_data&&d.last_sent_data.length>0){populateWriteGrid(d.last_sent_data);}"
"rs();if(!si)si=setInterval(rs,1000);}else{r.innerHTML='<div class=\"e\">'+d.error+'</div>';}"
"}).catch(e=>{r.innerHTML='<div class=\"e\">'+e.message+'</div>';});"
"}"
"function cc(){"
"var ip=document.getElementById('ip').value,to=parseInt(document.getElementById('to').value),r=document.getElementById('cr');"
"r.innerHTML='Closing...';"
"fetch('/api/scanner/implicit/close',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({ip_address:ip,timeout_ms:to})})"
".then(x=>x.json()).then(d=>{"
"if(d.success){r.innerHTML='<div class=\"s\">Closed!</div>';document.getElementById('st').style.display='none';"
"if(si){clearInterval(si);si=null;}}else{r.innerHTML='<div class=\"e\">'+d.error+'</div>';}"
"}).catch(e=>{r.innerHTML='<div class=\"e\">'+e.message+'</div>';});"
"}"
"function rs(){"
"fetch('/api/scanner/implicit/status').then(x=>x.json()).then(d=>{"
"var sb=document.getElementById('sb');"
"if(d.is_open){"
"sb.innerHTML='<div class=\"si\"><strong>Status:</strong><span style=\"color:green\">OPEN</span></div>'"
"+'<div class=\"si\"><strong>IP:</strong>'+d.ip_address+'</div>'"
"+'<div class=\"si\"><strong>O->T:</strong>'+d.assembly_instance_consumed+'</div>'"
"+'<div class=\"si\"><strong>T->O:</strong>'+d.assembly_instance_produced+'</div>'"
"+'<div class=\"si\"><strong>Size O->T:</strong>'+d.assembly_data_size_consumed+'</div>'"
"+'<div class=\"si\"><strong>Size T->O:</strong>'+d.assembly_data_size_produced+'</div>'"
"+'<div class=\"si\"><strong>RPI:</strong>'+d.rpi_ms+'ms</div>'"
"+'<div class=\"si\"><strong>Mode:</strong>'+(d.exclusive_owner?'PTP (Exclusive)':'Non-PTP (Multicast)')+'</div>'"
"+'<div class=\"si\"><strong>Rx:</strong>'+d.last_received_length+'b</div>'"
"+'<div class=\"si\"><strong>Time:</strong>'+d.last_packet_time_ms+'ms</div>';"
"if(d.last_received_data&&d.last_received_data.length>0){updateReceiveGrid(d.last_received_data);}"
"else{updateReceiveGrid([]);}"
"}else{"
"sb.innerHTML='<div class=\"si\"><strong>Status:</strong><span style=\"color:red\">CLOSED</span></div>';"
"updateReceiveGrid([]);"
"if(si){clearInterval(si);si=null;}document.getElementById('st').style.display='none';"
"}"
"}).catch(e=>{if(si){clearInterval(si);si=null;}});"
"}"
"window.onload=function(){rs();};"
"</script>"
"</body>"
"</html>";

// GET /implicit - Serve Implicit Messaging Test page
static esp_err_t webui_implicit_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t html_len = strlen(implicit_page);
    ESP_LOGD(TAG, "Sending implicit messaging page, length: %zu bytes", html_len);
    
    // Send in chunks to handle large HTML content
    const size_t chunk_size = 4096;  // Send 4KB chunks
    size_t sent = 0;
    esp_err_t ret = ESP_OK;
    
    while (sent < html_len && ret == ESP_OK) {
        size_t to_send = (html_len - sent < chunk_size) ? (html_len - sent) : chunk_size;
        ret = httpd_resp_send_chunk(req, implicit_page + sent, to_send);
        if (ret == ESP_OK) {
            sent += to_send;
        }
    }
    
    if (ret == ESP_OK) {
        // Send final empty chunk to indicate end of response
        ret = httpd_resp_send_chunk(req, NULL, 0);
    }
    
    return ret;
}
#endif

// Network configuration page HTML
static const char network_config_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Network Config</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
".n span.active{background:#9e9e9e;opacity:0.6;display:block;margin:0;padding:8px 15px;color:#fff;border-radius:4px;text-align:center}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input,select{width:100%;max-width:300px;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px;margin-right:10px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
".static-config{display:none;margin-top:10px}"
"</style></head><body>"
"<div class=\"c\"><h1>Network Configuration</h1>"
"<div class=\"n\" id=\"nav\"></div>"
"<label>IP Configuration:</label>"
"<select id=\"ipMode\" onchange=\"toggleStatic()\">"
"<option value=\"dhcp\">DHCP (Automatic)</option>"
"<option value=\"static\">Static IP</option>"
"</select>"
"<div id=\"staticConfig\" class=\"static-config\">"
"<label>IP Address:</label><input type=\"text\" id=\"ip\" placeholder=\"192.168.1.100\">"
"<label>Netmask:</label><input type=\"text\" id=\"nm\" placeholder=\"255.255.255.0\">"
"<label>Gateway:</label><input type=\"text\" id=\"gw\" placeholder=\"192.168.1.1\">"
"<label>DNS Server 1:</label><input type=\"text\" id=\"dns1\" placeholder=\"8.8.8.8\">"
"<label>DNS Server 2 (optional):</label><input type=\"text\" id=\"dns2\" placeholder=\"8.8.4.4\">"
"</div>"
"<button onclick=\"saveConfig()\">Save Configuration</button>"
"<div id=\"r\"></div></div>"
"<script>"
"var currentIp='';var currentNm='';var currentGw='';"
"function toggleStatic(){var m=document.getElementById('ipMode').value;var s=document.getElementById('staticConfig');s.style.display=m==='static'?'block':'none';if(m==='static'){var ip=document.getElementById('ip');var nm=document.getElementById('nm');var gw=document.getElementById('gw');if(!ip.value&&currentIp){ip.value=currentIp;}if(!nm.value&&currentNm){nm.value=currentNm;}if(!gw.value&&currentGw){gw.value=currentGw;}}}"
"function loadConfig(){fetch('/api/network/config').then(r=>r.json()).then(d=>{currentIp=d.current_ip_address||'';currentNm=d.current_netmask||'';currentGw=d.current_gateway||'';var mode=d.use_dhcp?'dhcp':'static';document.getElementById('ipMode').value=mode;if(!d.use_dhcp){document.getElementById('ip').value=d.ip_address||'';document.getElementById('nm').value=d.netmask||'';document.getElementById('gw').value=d.gateway||'';document.getElementById('dns1').value=d.dns1||'';document.getElementById('dns2').value=d.dns2||'';}else{document.getElementById('ip').value='';document.getElementById('nm').value='';document.getElementById('gw').value='';document.getElementById('dns1').value='';document.getElementById('dns2').value='';}toggleStatic();}).catch(e=>document.getElementById('r').innerHTML='<div class=\"e\">Error loading config: '+e.message+'</div>');}"
"window.saveConfig=function(){var m=document.getElementById('ipMode').value;var d={use_dhcp:m==='dhcp'};if(m==='static'){d.ip_address=document.getElementById('ip').value;d.netmask=document.getElementById('nm').value;d.gateway=document.getElementById('gw').value;d.dns1=document.getElementById('dns1').value;d.dns2=document.getElementById('dns2').value;}"
"document.getElementById('r').innerHTML='<div class=\"i\">Saving...</div>';"
"fetch('/api/network/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)})"
".then(r=>r.json()).then(x=>{document.getElementById('r').innerHTML=x.success?'<div class=\"s\">'+x.message+'</div>':'<div class=\"e\">Error: '+(x.error||'Unknown')+'</div>';})"
".catch(e=>document.getElementById('r').innerHTML='<div class=\"e\">Error: '+e.message+'</div>');};"
"document.addEventListener('DOMContentLoaded',function(){var n=document.getElementById('nav');if(n){var h='<a href=\"/\">Assembly I/O</a>';"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"h+='<a href=\"/tags\">Read Tag</a>';h+='<a href=\"/write-tag\">Write Tag</a>';"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"h+='<a href=\"/implicit\">Implicit I/O</a>';"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
"h+='<span class=\"active\">Network</span>';"
"h+='<div style=\"margin-top:8px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px\">';"
"h+='<a href=\"/motoman-status\">Motoman Status</a>';"
"h+='<a href=\"/motoman-job\">Motoman Job</a>';"
"h+='<a href=\"/motoman-robot-position\">Motoman Position</a>';"
"h+='<a href=\"/motoman-position-deviation\">Motoman Deviation</a>';"
"h+='<a href=\"/motoman-torque\">Motoman Torque</a>';"
"h+='<a href=\"/motoman-io\">Motoman I/O</a>';"
"h+='<a href=\"/motoman-register\">Motoman Register</a>';"
"h+='<a href=\"/motoman-variable-b\">Motoman Var B</a>';"
"h+='<a href=\"/motoman-variable-i\">Motoman Var I</a>';"
"h+='<a href=\"/motoman-variable-d\">Motoman Var D</a>';"
"h+='<a href=\"/motoman-variable-r\">Motoman Var R</a>';"
"h+='<a href=\"/motoman-variable-s\">Motoman Var S</a>';"
"h+='<a href=\"/motoman-position\">Motoman Var P</a>';"
"h+='<a href=\"/motoman-alarms\">Motoman Alarms</a>';"
"h+='</div>';n.innerHTML=h;}loadConfig();});"
#else
"h+='<span class=\"active\">Network</span>';n.innerHTML=h;}loadConfig();});"
#endif
IP_PERSIST_SCRIPT
"</script></body></html>";

#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
// Motoman Position Variable page HTML
static const char motoman_position_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman Position Variable</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:1000px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
".n span.active{background:#9e9e9e;opacity:0.6;display:block;margin:0;padding:8px 15px;color:#fff;border-radius:4px;text-align:center}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input,select{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:150px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td{padding:8px;border-bottom:1px solid #eee}"
"table td:first-child{font-weight:bold;width:200px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman Position Variable</h1>"
"<div class=\"n\" id=\"nav\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"ipAddress\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Position Variable:</label>"
"<select id=\"variableNumber\" style=\"max-width:200px\">"
"<option value=\"0\">P0</option>"
"<option value=\"1\">P1</option>"
"<option value=\"2\">P2</option>"
"<option value=\"3\">P3</option>"
"<option value=\"4\">P4</option>"
"<option value=\"5\">P5</option>"
"<option value=\"6\">P6</option>"
"<option value=\"7\">P7</option>"
"<option value=\"8\">P8</option>"
"<option value=\"9\">P9</option>"
"<option value=\"10\">P10</option>"
"</select>"
"<label>Timeout (ms):</label><input type=\"number\" id=\"timeout\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\" style=\"max-width:150px\">"
"<button onclick=\"readPosition()\">Read Position Variable</button>"
"<div class=\"i\" style=\"margin-top:8px\">Note: Instance mapping follows RS022 setting on the Status page.</div>"
"<div id=\"results\"></div></div>"
"<script>"
"function readPosition(){"
"var ip=document.getElementById('ipAddress').value;"
"var varNum=parseInt(document.getElementById('variableNumber').value);"
"var to=parseInt(document.getElementById('timeout').value);"
"var r=document.getElementById('results');"
"if(!ip){r.innerHTML='<div class=\"e\">Please enter IP address</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading position variable...</div>';"
"fetch('/api/scanner/motoman/read-position-variable',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,variable_number:varNum,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">Position variable read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Variable:</td><td>P'+d.variable_number+'</td></tr>';"
"var dataTypeNames={0:'Pulse',16:'Base',17:'Robot',18:'Tool',19:'User coordinates'};"
"h+='<tr><td>Data Type:</td><td>'+(dataTypeNames[d.data_type]||'Unknown ('+d.data_type+')')+'</td></tr>';"
"h+='<tr><td>Configuration:</td><td>0x'+d.configuration.toString(16).toUpperCase()+'</td></tr>';"
"h+='<tr><td>Tool Number:</td><td>'+d.tool_number+'</td></tr>';"
"h+='<tr><td>User Coordinate:</td><td>'+d.user_coordinate_number+'</td></tr>';"
"h+='<tr><td>Extended Config:</td><td>0x'+d.extended_configuration.toString(16).toUpperCase()+'</td></tr>';"
"h+='<tr><td>Axis Data:</td><td></td></tr>';"
"if(d.axis_data&&d.axis_data.length>0){"
"h+='<tr><td colspan=\"2\"><table style=\"width:100%;margin-top:5px\">';"
"h+='<tr><th style=\"text-align:left;width:20%\">Axis</th><th style=\"text-align:left;width:40%\">Raw</th><th style=\"text-align:left;width:40%\">Scaled</th></tr>';"
"var hasEngineeringUnits=(d.data_type===16||d.data_type===17||d.data_type===18||d.data_type===19);"
"for(var i=0;i<d.axis_data.length;i++){"
"var rawVal=d.axis_data[i];"
"var scaledVal=rawVal;"
"var unit='';"
"if(hasEngineeringUnits&&i<3){scaledVal=(rawVal/1000.0).toFixed(3);unit=' mm';}"
"else if(hasEngineeringUnits&&i>=3&&i<6){scaledVal=(rawVal/10000.0).toFixed(4);unit=' deg';}"
"else if(hasEngineeringUnits&&i>=6){scaledVal=(rawVal/1000.0).toFixed(3);unit=' mm';}"
"h+='<tr><td>Axis '+(i+1)+'</td><td>'+rawVal+'</td><td>';"
"if(hasEngineeringUnits){h+=scaledVal+unit;}else{h+='-';}"
"h+='</td></tr>';"
"}"
"h+='</table></td></tr>';"
"}"
"h+='</table></div>';"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
IP_PERSIST_SCRIPT
"</script></body></html>";

// Motoman Alarm page HTML
static const char motoman_alarm_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman Alarms</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:900px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
".n span.active{background:#9e9e9e;opacity:0.6;display:block;margin:0;padding:8px 15px;color:#fff;border-radius:4px;text-align:center}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input,select{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:180px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td{padding:8px;border-bottom:1px solid #eee}"
"table td:first-child{font-weight:bold;width:220px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman Alarms</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"alarmIp\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Alarm Type:</label>"
"<select id=\"alarmType\" style=\"max-width:240px\" onchange=\"updateAlarmHint()\">"
"<option value=\"current\">Current (Class 0x70)</option>"
"<option value=\"history\">History (Class 0x71)</option>"
"</select>"
"<label>Alarm Instance:</label>"
"<input type=\"number\" id=\"alarmInstance\" placeholder=\"1\" value=\"1\" min=\"1\" max=\"4100\">"
"<div class=\"i\" id=\"alarmHint\" style=\"margin-top:-5px\">Current alarms: instances 1-4 (1=latest).</div>"
"<label>Timeout (ms):</label><input type=\"number\" id=\"alarmTimeout\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\">"
"<button onclick=\"readAlarm()\">Read Alarm</button><div id=\"alarmResults\"></div></div>"
"<script>"
"function updateAlarmHint(){"
"var t=document.getElementById('alarmType').value;"
"var h=document.getElementById('alarmHint');"
"if(t==='history'){"
"h.textContent='History instances: 1-100 Major, 1001-1100 Minor, 2001-2100 User(System), 3001-3100 User(User), 4001-4100 Off-line.';"
"}else{"
"h.textContent='Current alarms: instances 1-4 (1=latest).';"
"}"
"}"
"function readAlarm(){"
"var ip=document.getElementById('alarmIp').value;"
"var t=document.getElementById('alarmType').value;"
"var inst=parseInt(document.getElementById('alarmInstance').value);"
"var to=parseInt(document.getElementById('alarmTimeout').value);"
"var r=document.getElementById('alarmResults');"
"if(!ip||!inst){r.innerHTML='<div class=\"e\">Please enter IP address and instance</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading alarm...</div>';"
"fetch('/api/scanner/motoman/read-alarm',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,alarm_type:t,alarm_instance:inst,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">Alarm read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Alarm Type:</td><td>'+d.alarm_type+'</td></tr>';"
"h+='<tr><td>Alarm Instance:</td><td>'+d.alarm_instance+'</td></tr>';"
"h+='<tr><td>Alarm Code:</td><td>'+d.alarm_code+'</td></tr>';"
"h+='<tr><td>Alarm Data:</td><td>'+d.alarm_data+'</td></tr>';"
"h+='<tr><td>Alarm Data Type:</td><td>'+d.alarm_data_type+'</td></tr>';"
"h+='<tr><td>Date/Time:</td><td>'+(d.alarm_date_time||'')+'</td></tr>';"
"h+='<tr><td>Alarm String:</td><td>'+(d.alarm_string||'')+'</td></tr>';"
"h+='</table></div>';"
"r.innerHTML=h;"
"fetch('/api/scanner/motoman/read-status',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(s){"
"if(!s.success){return;}"
"var sh='<div class=\"data-table\"><table>';"
"sh+='<tr><td>Status Data1:</td><td>0x'+s.data1.toString(16).toUpperCase()+'</td></tr>';"
"sh+='<tr><td>Status Data2:</td><td>0x'+s.data2.toString(16).toUpperCase()+'</td></tr>';"
"sh+='<tr><td>Hold (External):</td><td>'+(s.hold_external?'Yes':'No')+'</td></tr>';"
"sh+='<tr><td>Hold (Pendant):</td><td>'+(s.hold_pendant?'Yes':'No')+'</td></tr>';"
"sh+='<tr><td>Hold (Command):</td><td>'+(s.hold_command?'Yes':'No')+'</td></tr>';"
"sh+='<tr><td>Alarm Bit:</td><td>'+(s.alarm?'Yes':'No')+'</td></tr>';"
"sh+='<tr><td>Error Bit:</td><td>'+(s.error?'Yes':'No')+'</td></tr>';"
"sh+='<tr><td>Servo On:</td><td>'+(s.servo_on?'Yes':'No')+'</td></tr>';"
"sh+='</table></div>';"
"r.innerHTML+=sh;"
"if(d.alarm_code===0&&d.alarm_string===''){"
"var msg='No current alarm text returned. The controller may be reporting an external hold/estop rather than a Class 0x70 alarm.';"
"r.innerHTML+='<div class=\"i\">'+msg+'</div>';"
"}"
"})"
".catch(function(){/* ignore status errors */});"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
"document.addEventListener('DOMContentLoaded',updateAlarmHint);"
IP_PERSIST_SCRIPT
"</script></body></html>";

static const char motoman_status_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman Status</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:150px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td{padding:8px;border-bottom:1px solid #eee}"
"table td:first-child{font-weight:bold;width:220px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman Status</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"statusIp\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Timeout (ms):</label><input type=\"number\" id=\"statusTimeout\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\">"
"<div class=\"i\" style=\"margin-top:8px;display:flex;flex-wrap:wrap;align-items:center;gap:12px\">"
"<span>RS022 Mapping:</span>"
"<label style=\"display:inline-flex;align-items:center;gap:6px;margin:0\">"
"<input type=\"checkbox\" id=\"rs022Toggle\">"
"<span>Instance = variable/register number (RS022=1)</span>"
"</label>"
"<button onclick=\"saveRs022()\" style=\"margin:0\">Save RS022</button>"
"</div>"
"<button onclick=\"readStatus()\">Read Status</button><div id=\"statusResults\"></div></div>"
"<script>"
"function loadRs022(){"
"fetch('/api/scanner/motoman/rs022').then(function(x){return x.json();}).then(function(d){"
"if(d.success){document.getElementById('rs022Toggle').checked=!!d.instance_direct;}"
"}).catch(function(){});"
"}"
"function saveRs022(){"
"var val=document.getElementById('rs022Toggle').checked;"
"fetch('/api/scanner/motoman/rs022',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({instance_direct:val})})"
".then(function(x){return x.json();}).then(function(d){"
"var r=document.getElementById('statusResults');"
"if(d.success){r.innerHTML='<div class=\"s\">RS022 saved. Instance direct = '+(d.instance_direct?'true':'false')+'</div>'+r.innerHTML;}"
"else{r.innerHTML='<div class=\"e\">Failed to save RS022</div>'+r.innerHTML;}"
"}).catch(function(e){var r=document.getElementById('statusResults');r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>'+r.innerHTML;});"
"}"
"function readStatus(){"
"var ip=document.getElementById('statusIp').value;"
"var to=parseInt(document.getElementById('statusTimeout').value);"
"var r=document.getElementById('statusResults');"
"if(!ip){r.innerHTML='<div class=\"e\">Please enter IP address</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading status...</div>';"
"fetch('/api/scanner/motoman/read-status',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">Status read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Data1:</td><td>0x'+d.data1.toString(16).toUpperCase()+'</td></tr>';"
"h+='<tr><td>Data2:</td><td>0x'+d.data2.toString(16).toUpperCase()+'</td></tr>';"
"h+='<tr><td>Hold (Pendant):</td><td>'+(d.hold_pendant?'Yes':'No')+'</td></tr>';"
"h+='<tr><td>Hold (External):</td><td>'+(d.hold_external?'Yes':'No')+'</td></tr>';"
"h+='<tr><td>Hold (Command):</td><td>'+(d.hold_command?'Yes':'No')+'</td></tr>';"
"h+='<tr><td>Alarm Bit:</td><td>'+(d.alarm?'Yes':'No')+'</td></tr>';"
"h+='<tr><td>Error Bit:</td><td>'+(d.error?'Yes':'No')+'</td></tr>';"
"h+='<tr><td>Servo On:</td><td>'+(d.servo_on?'Yes':'No')+'</td></tr>';"
"h+='</table></div>';"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
"document.addEventListener('DOMContentLoaded',loadRs022);"
IP_PERSIST_SCRIPT
"</script></body></html>";

static const char motoman_job_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman Job Info</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:150px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td{padding:8px;border-bottom:1px solid #eee}"
"table td:first-child{font-weight:bold;width:220px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman Job Info</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"jobIp\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Timeout (ms):</label><input type=\"number\" id=\"jobTimeout\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\">"
"<button onclick=\"readJob()\">Read Job Info</button><div id=\"jobResults\"></div></div>"
"<script>"
"function readJob(){"
"var ip=document.getElementById('jobIp').value;"
"var to=parseInt(document.getElementById('jobTimeout').value);"
"var r=document.getElementById('jobResults');"
"if(!ip){r.innerHTML='<div class=\"e\">Please enter IP address</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading job info...</div>';"
"fetch('/api/scanner/motoman/read-job-info',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,timeout_ms:to})})"
".then(function(x){return x.text().then(function(t){var j=null;try{j=JSON.parse(t);}catch(e){}return {ok:x.ok,data:j,text:t};});})"
".then(function(resp){"
"if(!resp.ok||!resp.data){r.innerHTML='<div class=\"e\">Read failed: '+(resp.text||'Unknown error')+'</div>';return;}"
"var d=resp.data;"
"if(d.success){"
"var h='<div class=\"s\">Job info read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Job Name:</td><td>'+d.job_name+'</td></tr>';"
"h+='<tr><td>Line Number:</td><td>'+d.line_number+'</td></tr>';"
"h+='<tr><td>Step Number:</td><td>'+d.step_number+'</td></tr>';"
"h+='<tr><td>Speed Override:</td><td>'+d.speed_override+'</td></tr>';"
"h+='</table></div>';"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
IP_PERSIST_SCRIPT
"</script></body></html>";

static const char motoman_robot_position_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman Robot Position</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:1000px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:200px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td,table th{padding:8px;border-bottom:1px solid #eee;text-align:left}"
"table th{background:#f7f7f7}"
"table td:first-child{font-weight:bold;width:200px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman Robot Position</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"posIp\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Control Group:</label><input type=\"number\" id=\"posGroup\" placeholder=\"1\" value=\"1\" min=\"1\" max=\"118\">"
"<label>Timeout (ms):</label><input type=\"number\" id=\"posTimeout\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\">"
"<button onclick=\"readRobotPosition()\">Read Robot Position</button><div id=\"posResults\"></div></div>"
"<script>"
"function readRobotPosition(){"
"var ip=document.getElementById('posIp').value;"
"var group=parseInt(document.getElementById('posGroup').value);"
"var to=parseInt(document.getElementById('posTimeout').value);"
"var r=document.getElementById('posResults');"
"if(!ip||!group){r.innerHTML='<div class=\"e\">Please enter IP and control group</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading position...</div>';"
"fetch('/api/scanner/motoman/read-position',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,control_group:group,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">Position read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Control Group:</td><td>'+d.control_group+'</td></tr>';"
"var dataTypeNames={0:'Pulse',16:'Base',17:'Robot',18:'Tool',19:'User coordinates'};"
"h+='<tr><td>Data Type:</td><td>'+(dataTypeNames[d.data_type]||('Unknown ('+d.data_type+')'))+'</td></tr>';"
"h+='<tr><td>Configuration:</td><td>0x'+d.configuration.toString(16).toUpperCase()+'</td></tr>';"
"h+='<tr><td>Tool Number:</td><td>'+d.tool_number+'</td></tr>';"
"h+='<tr><td>Reservation:</td><td>'+d.reservation+'</td></tr>';"
"h+='<tr><td>Extended Config:</td><td>0x'+d.extended_configuration.toString(16).toUpperCase()+'</td></tr>';"
"h+='</table></div>';"
"if(d.axis_data&&d.axis_data.length>0){"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><th style=\"width:20%\">Axis</th><th style=\"width:40%\">Raw</th><th style=\"width:40%\">Scaled</th></tr>';"
"var hasEngineeringUnits=(d.data_type===16||d.data_type===17||d.data_type===18||d.data_type===19);"
"for(var i=0;i<d.axis_data.length;i++){"
"var rawVal=d.axis_data[i];"
"var scaledVal=rawVal;var unit='';"
"if(hasEngineeringUnits&&i<3){scaledVal=(rawVal/1000.0).toFixed(3);unit=' mm';}"
"else if(hasEngineeringUnits&&i>=3&&i<6){scaledVal=(rawVal/10000.0).toFixed(4);unit=' deg';}"
"else if(hasEngineeringUnits&&i>=6){scaledVal=(rawVal/1000.0).toFixed(3);unit=' mm';}"
"h+='<tr><td>Axis '+(i+1)+'</td><td>'+rawVal+'</td><td>'+(hasEngineeringUnits?(scaledVal+unit):'-')+'</td></tr>';"
"}"
"h+='</table></div>';"
"}"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
IP_PERSIST_SCRIPT
"</script></body></html>";

static const char motoman_position_deviation_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman Position Deviation</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:200px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td{padding:8px;border-bottom:1px solid #eee}"
"table td:first-child{font-weight:bold;width:200px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman Position Deviation</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"devIp\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Control Group:</label><input type=\"number\" id=\"devGroup\" placeholder=\"1\" value=\"1\" min=\"1\" max=\"44\">"
"<label>Timeout (ms):</label><input type=\"number\" id=\"devTimeout\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\">"
"<button onclick=\"readDeviation()\">Read Deviation</button><div id=\"devResults\"></div></div>"
"<script>"
"function readDeviation(){"
"var ip=document.getElementById('devIp').value;"
"var group=parseInt(document.getElementById('devGroup').value);"
"var to=parseInt(document.getElementById('devTimeout').value);"
"var r=document.getElementById('devResults');"
"if(!ip||!group){r.innerHTML='<div class=\"e\">Please enter IP and control group</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading deviation...</div>';"
"fetch('/api/scanner/motoman/read-position-deviation',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,control_group:group,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">Deviation read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Control Group:</td><td>'+d.control_group+'</td></tr>';"
"h+='</table></div>';"
"if(d.axis_deviation&&d.axis_deviation.length>0){"
"h+='<div class=\"data-table\"><table>';"
"for(var i=0;i<d.axis_deviation.length;i++){"
"h+='<tr><td>Axis '+(i+1)+':</td><td>'+d.axis_deviation[i]+'</td></tr>';"
"}"
"h+='</table></div>';"
"}"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
IP_PERSIST_SCRIPT
"</script></body></html>";

static const char motoman_torque_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman Torque</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:200px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td{padding:8px;border-bottom:1px solid #eee}"
"table td:first-child{font-weight:bold;width:200px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman Torque</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"torqueIp\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Control Group:</label><input type=\"number\" id=\"torqueGroup\" placeholder=\"1\" value=\"1\" min=\"1\" max=\"44\">"
"<label>Timeout (ms):</label><input type=\"number\" id=\"torqueTimeout\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\">"
"<button onclick=\"readTorque()\">Read Torque</button><div id=\"torqueResults\"></div></div>"
"<script>"
"function readTorque(){"
"var ip=document.getElementById('torqueIp').value;"
"var group=parseInt(document.getElementById('torqueGroup').value);"
"var to=parseInt(document.getElementById('torqueTimeout').value);"
"var r=document.getElementById('torqueResults');"
"if(!ip||!group){r.innerHTML='<div class=\"e\">Please enter IP and control group</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading torque...</div>';"
"fetch('/api/scanner/motoman/read-torque',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,control_group:group,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">Torque read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Control Group:</td><td>'+d.control_group+'</td></tr>';"
"h+='</table></div>';"
"if(d.axis_torque&&d.axis_torque.length>0){"
"h+='<div class=\"data-table\"><table>';"
"for(var i=0;i<d.axis_torque.length;i++){"
"h+='<tr><td>Axis '+(i+1)+':</td><td>'+d.axis_torque[i]+'</td></tr>';"
"}"
"h+='</table></div>';"
"}"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
IP_PERSIST_SCRIPT
"</script></body></html>";

static const char motoman_io_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman I/O</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:200px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td{padding:8px;border-bottom:1px solid #eee}"
"table td:first-child{font-weight:bold;width:220px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman I/O</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"ioIp\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Signal Number:</label><input type=\"number\" id=\"ioSignal\" placeholder=\"1\" value=\"1\" min=\"1\" max=\"8220\">"
"<label>Timeout (ms):</label><input type=\"number\" id=\"ioTimeout\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\">"
"<button onclick=\"readIo()\">Read I/O</button><div id=\"ioResults\"></div></div>"
"<script>"
"function readIo(){"
"var ip=document.getElementById('ioIp').value;"
"var signal=parseInt(document.getElementById('ioSignal').value);"
"var to=parseInt(document.getElementById('ioTimeout').value);"
"var r=document.getElementById('ioResults');"
"if(!ip||!signal){r.innerHTML='<div class=\"e\">Please enter IP and signal number</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading I/O...</div>';"
"fetch('/api/scanner/motoman/read-io',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,signal_number:signal,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">I/O read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Signal Number:</td><td>'+d.signal_number+'</td></tr>';"
"h+='<tr><td>Value:</td><td>'+d.value+'</td></tr>';"
"h+='</table></div>';"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
IP_PERSIST_SCRIPT
"</script></body></html>";

static const char motoman_register_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman Register</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:200px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td{padding:8px;border-bottom:1px solid #eee}"
"table td:first-child{font-weight:bold;width:220px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman Register</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"regIp\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Register Number:</label><input type=\"number\" id=\"regNumber\" placeholder=\"0\" value=\"0\" min=\"0\" max=\"999\">"
"<label>Timeout (ms):</label><input type=\"number\" id=\"regTimeout\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\">"
"<button onclick=\"readRegister()\">Read Register</button><div id=\"regResults\"></div></div>"
"<script>"
"function readRegister(){"
"var ip=document.getElementById('regIp').value;"
"var reg=parseInt(document.getElementById('regNumber').value);"
"var to=parseInt(document.getElementById('regTimeout').value);"
"var r=document.getElementById('regResults');"
"if(!ip||reg<0){r.innerHTML='<div class=\"e\">Please enter IP and register number</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading register...</div>';"
"fetch('/api/scanner/motoman/read-register',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,register_number:reg,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">Register read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Register Number:</td><td>'+d.register_number+'</td></tr>';"
"h+='<tr><td>Value:</td><td>'+d.value+'</td></tr>';"
"h+='</table></div>';"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
IP_PERSIST_SCRIPT
"</script></body></html>";

static const char motoman_variable_b_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman Variable B</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:200px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td{padding:8px;border-bottom:1px solid #eee}"
"table td:first-child{font-weight:bold;width:220px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman Variable B</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"varBip\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Variable Number (0-based):</label><input type=\"number\" id=\"varBnum\" placeholder=\"0\" value=\"0\" min=\"0\" max=\"65535\">"
"<label>Timeout (ms):</label><input type=\"number\" id=\"varBto\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\">"
"<button onclick=\"readVarB()\">Read Variable B</button><div id=\"varBResults\"></div></div>"
"<script>"
"function readVarB(){"
"var ip=document.getElementById('varBip').value;"
"var num=parseInt(document.getElementById('varBnum').value);"
"var to=parseInt(document.getElementById('varBto').value);"
"var r=document.getElementById('varBResults');"
"if(!ip){r.innerHTML='<div class=\"e\">Please enter IP address</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading variable B...</div>';"
"fetch('/api/scanner/motoman/read-variable-b',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,variable_number:num,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">Variable B read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Variable Number:</td><td>'+d.variable_number+'</td></tr>';"
"h+='<tr><td>Value:</td><td>'+d.value+'</td></tr>';"
"h+='</table></div>';"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
IP_PERSIST_SCRIPT
"</script></body></html>";

static const char motoman_variable_i_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman Variable I</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:200px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td{padding:8px;border-bottom:1px solid #eee}"
"table td:first-child{font-weight:bold;width:220px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman Variable I</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"varIip\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Variable Number (0-based):</label><input type=\"number\" id=\"varInum\" placeholder=\"0\" value=\"0\" min=\"0\" max=\"65535\">"
"<label>Timeout (ms):</label><input type=\"number\" id=\"varIto\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\">"
"<button onclick=\"readVarI()\">Read Variable I</button><div id=\"varIResults\"></div></div>"
"<script>"
"function readVarI(){"
"var ip=document.getElementById('varIip').value;"
"var num=parseInt(document.getElementById('varInum').value);"
"var to=parseInt(document.getElementById('varIto').value);"
"var r=document.getElementById('varIResults');"
"if(!ip){r.innerHTML='<div class=\"e\">Please enter IP address</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading variable I...</div>';"
"fetch('/api/scanner/motoman/read-variable-i',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,variable_number:num,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">Variable I read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Variable Number:</td><td>'+d.variable_number+'</td></tr>';"
"h+='<tr><td>Value:</td><td>'+d.value+'</td></tr>';"
"h+='</table></div>';"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
IP_PERSIST_SCRIPT
"</script></body></html>";

static const char motoman_variable_d_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman Variable D</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:200px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td{padding:8px;border-bottom:1px solid #eee}"
"table td:first-child{font-weight:bold;width:220px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman Variable D</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"varDip\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Variable Number (0-based):</label><input type=\"number\" id=\"varDnum\" placeholder=\"0\" value=\"0\" min=\"0\" max=\"65535\">"
"<label>Timeout (ms):</label><input type=\"number\" id=\"varDto\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\">"
"<button onclick=\"readVarD()\">Read Variable D</button><div id=\"varDResults\"></div></div>"
"<script>"
"function readVarD(){"
"var ip=document.getElementById('varDip').value;"
"var num=parseInt(document.getElementById('varDnum').value);"
"var to=parseInt(document.getElementById('varDto').value);"
"var r=document.getElementById('varDResults');"
"if(!ip){r.innerHTML='<div class=\"e\">Please enter IP address</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading variable D...</div>';"
"fetch('/api/scanner/motoman/read-variable-d',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,variable_number:num,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">Variable D read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Variable Number:</td><td>'+d.variable_number+'</td></tr>';"
"h+='<tr><td>Value:</td><td>'+d.value+'</td></tr>';"
"h+='</table></div>';"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
IP_PERSIST_SCRIPT
"</script></body></html>";

static const char motoman_variable_r_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman Variable R</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:200px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td{padding:8px;border-bottom:1px solid #eee}"
"table td:first-child{font-weight:bold;width:220px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman Variable R</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"varRip\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Variable Number (0-based):</label><input type=\"number\" id=\"varRnum\" placeholder=\"0\" value=\"0\" min=\"0\" max=\"65535\">"
"<label>Timeout (ms):</label><input type=\"number\" id=\"varRto\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\">"
"<button onclick=\"readVarR()\">Read Variable R</button><div id=\"varRResults\"></div></div>"
"<script>"
"function readVarR(){"
"var ip=document.getElementById('varRip').value;"
"var num=parseInt(document.getElementById('varRnum').value);"
"var to=parseInt(document.getElementById('varRto').value);"
"var r=document.getElementById('varRResults');"
"if(!ip){r.innerHTML='<div class=\"e\">Please enter IP address</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading variable R...</div>';"
"fetch('/api/scanner/motoman/read-variable-r',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,variable_number:num,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">Variable R read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Variable Number:</td><td>'+d.variable_number+'</td></tr>';"
"h+='<tr><td>Value:</td><td>'+d.value+'</td></tr>';"
"h+='</table></div>';"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
IP_PERSIST_SCRIPT
"</script></body></html>";

static const char motoman_variable_s_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Motoman Variable S</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px;display:grid;grid-template-columns:repeat(4,1fr);gap:6px}"
".n a{display:block;margin:0;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px;text-align:center}"
".n > div{grid-column:1/-1}"
".n a:hover{background:#45a049}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"input[type=number]{max-width:200px}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"table{width:100%;border-collapse:collapse;margin:10px 0}"
"table td{padding:8px;border-bottom:1px solid #eee}"
"table td:first-child{font-weight:bold;width:220px;color:#555}"
".data-table{margin-top:15px;background:#fff;border:1px solid #ddd;border-radius:4px;overflow:hidden}"
"</style></head><body>"
"<div class=\"c\"><h1>Motoman Variable S</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
"<a href=\"/tags\">Read Tag</a><a href=\"/write-tag\">Write Tag</a>"
#endif
#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
"<a href=\"/implicit\">Implicit I/O</a>"
#endif
"<a href=\"/network\">Network</a>"
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
MOTOMAN_NAV_ROW
#endif
"</div>"
"<label>IP Address:</label><input type=\"text\" id=\"varSip\" placeholder=\"192.168.1.100\" value=\"\">"
"<label>Variable Number (0-based):</label><input type=\"number\" id=\"varSnum\" placeholder=\"0\" value=\"0\" min=\"0\" max=\"65535\">"
"<label>Timeout (ms):</label><input type=\"number\" id=\"varSto\" placeholder=\"5000\" value=\"5000\" min=\"1000\" max=\"30000\">"
"<button onclick=\"readVarS()\">Read Variable S</button><div id=\"varSResults\"></div></div>"
"<script>"
"function readVarS(){"
"var ip=document.getElementById('varSip').value;"
"var num=parseInt(document.getElementById('varSnum').value);"
"var to=parseInt(document.getElementById('varSto').value);"
"var r=document.getElementById('varSResults');"
"if(!ip){r.innerHTML='<div class=\"e\">Please enter IP address</div>';return;}"
"r.innerHTML='<div class=\"i\">Reading variable S...</div>';"
"fetch('/api/scanner/motoman/read-variable-s',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,variable_number:num,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(d){"
"if(d.success){"
"var h='<div class=\"s\">Variable S read successful!</div>';"
"h+='<div class=\"data-table\"><table>';"
"h+='<tr><td>IP Address:</td><td>'+d.ip_address+'</td></tr>';"
"h+='<tr><td>Variable Number:</td><td>'+d.variable_number+'</td></tr>';"
"h+='<tr><td>Value:</td><td>'+d.value+'</td></tr>';"
"h+='</table></div>';"
"r.innerHTML=h;"
"}else{"
"r.innerHTML='<div class=\"e\">Read failed: '+(d.error||'Unknown error')+'</div>';"
"}"
"})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
IP_PERSIST_SCRIPT
"</script></body></html>";


// GET /motoman-position - Serve Motoman Position Variable page
static esp_err_t webui_motoman_position_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t html_len = strlen(motoman_position_page);
    ESP_LOGD(TAG, "Sending Motoman position page, length: %zu bytes", html_len);
    
    // Send in chunks to handle large HTML content
    const size_t chunk_size = 4096;  // Send 4KB chunks
    size_t sent = 0;
    esp_err_t ret = ESP_OK;
    
    while (sent < html_len && ret == ESP_OK) {
        size_t to_send = (html_len - sent < chunk_size) ? (html_len - sent) : chunk_size;
        ret = httpd_resp_send_chunk(req, motoman_position_page + sent, to_send);
        if (ret == ESP_OK) {
            sent += to_send;
        }
    }
    
    if (ret == ESP_OK) {
        // Send final empty chunk to indicate end of response
        ret = httpd_resp_send_chunk(req, NULL, 0);
    }
    
    return ret;
}

// GET /motoman-alarms - Serve Motoman Alarm page
static esp_err_t webui_motoman_alarm_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t html_len = strlen(motoman_alarm_page);
    esp_err_t ret = ESP_OK;
    
    // Send in chunks to avoid HTTPD_RESP_USE_STRLEN
    const size_t chunk_size = 1024;
    size_t sent = 0;
    
    while (sent < html_len) {
        size_t to_send = (html_len - sent) > chunk_size ? chunk_size : (html_len - sent);
        ret = httpd_resp_send_chunk(req, motoman_alarm_page + sent, to_send);
        if (ret != ESP_OK) {
            break;
        }
        sent += to_send;
    }
    
    if (ret == ESP_OK) {
        ret = httpd_resp_send_chunk(req, NULL, 0);
    }
    
    return ret;
}

static esp_err_t webui_send_page(httpd_req_t *req, const char *page)
{
    httpd_resp_set_type(req, "text/html");
    size_t html_len = strlen(page);
    esp_err_t ret = ESP_OK;
    const size_t chunk_size = 1024;
    size_t sent = 0;
    
    while (sent < html_len) {
        size_t to_send = (html_len - sent) > chunk_size ? chunk_size : (html_len - sent);
        ret = httpd_resp_send_chunk(req, page + sent, to_send);
        if (ret != ESP_OK) {
            break;
        }
        sent += to_send;
    }
    
    if (ret == ESP_OK) {
        ret = httpd_resp_send_chunk(req, NULL, 0);
    }
    
    return ret;
}

static esp_err_t webui_motoman_status_handler(httpd_req_t *req)
{
    return webui_send_page(req, motoman_status_page);
}

static esp_err_t webui_motoman_job_handler(httpd_req_t *req)
{
    return webui_send_page(req, motoman_job_page);
}

static esp_err_t webui_motoman_robot_position_handler(httpd_req_t *req)
{
    return webui_send_page(req, motoman_robot_position_page);
}

static esp_err_t webui_motoman_position_deviation_handler(httpd_req_t *req)
{
    return webui_send_page(req, motoman_position_deviation_page);
}

static esp_err_t webui_motoman_torque_handler(httpd_req_t *req)
{
    return webui_send_page(req, motoman_torque_page);
}

static esp_err_t webui_motoman_io_handler(httpd_req_t *req)
{
    return webui_send_page(req, motoman_io_page);
}

static esp_err_t webui_motoman_register_handler(httpd_req_t *req)
{
    return webui_send_page(req, motoman_register_page);
}

static esp_err_t webui_motoman_variable_b_handler(httpd_req_t *req)
{
    return webui_send_page(req, motoman_variable_b_page);
}

static esp_err_t webui_motoman_variable_i_handler(httpd_req_t *req)
{
    return webui_send_page(req, motoman_variable_i_page);
}

static esp_err_t webui_motoman_variable_d_handler(httpd_req_t *req)
{
    return webui_send_page(req, motoman_variable_d_page);
}

static esp_err_t webui_motoman_variable_r_handler(httpd_req_t *req)
{
    return webui_send_page(req, motoman_variable_r_page);
}

static esp_err_t webui_motoman_variable_s_handler(httpd_req_t *req)
{
    return webui_send_page(req, motoman_variable_s_page);
}
#endif // CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT

static esp_err_t webui_network_config_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t html_len = strlen(network_config_page);
    ESP_LOGD(TAG, "Sending network config page, length: %zu bytes", html_len);
    
    // Send in chunks to handle large HTML content
    const size_t chunk_size = 4096;  // Send 4KB chunks
    size_t sent = 0;
    esp_err_t ret = ESP_OK;
    
    while (sent < html_len && ret == ESP_OK) {
        size_t to_send = (html_len - sent < chunk_size) ? (html_len - sent) : chunk_size;
        ret = httpd_resp_send_chunk(req, network_config_page + sent, to_send);
        if (ret == ESP_OK) {
            sent += to_send;
        }
    }
    
    if (ret == ESP_OK) {
        // Send final empty chunk to indicate end of response
        ret = httpd_resp_send_chunk(req, NULL, 0);
    }
    
    return ret;
}

// Register HTML page handlers
esp_err_t webui_html_register(httpd_handle_t server)
{
    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = webui_index_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &index_uri);
    
    // Also register /write for backwards compatibility
    httpd_uri_t write_uri = {
        .uri = "/write",
        .method = HTTP_GET,
        .handler = webui_index_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &write_uri);
    
    ESP_LOGI(TAG, "Web UI HTML pages registered (/, /write)");
#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
    httpd_uri_t tags_uri = {
        .uri = "/tags",
        .method = HTTP_GET,
        .handler = webui_tags_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &tags_uri);
    ESP_LOGI(TAG, "Tag test page registered (/tags)");
    
    httpd_uri_t write_tags_uri = {
        .uri = "/write-tag",
        .method = HTTP_GET,
        .handler = webui_write_tags_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &write_tags_uri);
    ESP_LOGI(TAG, "Write tag page registered (/write-tag)");
#endif

#if CONFIG_ENIP_SCANNER_ENABLE_IMPLICIT_SUPPORT
    httpd_uri_t implicit_uri = {
        .uri = "/implicit",
        .method = HTTP_GET,
        .handler = webui_implicit_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &implicit_uri);
    ESP_LOGI(TAG, "Implicit messaging test page registered (/implicit)");
#endif
    
    httpd_uri_t network_config_uri = {
        .uri = "/network",
        .method = HTTP_GET,
        .handler = webui_network_config_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &network_config_uri);
    ESP_LOGI(TAG, "Network config page registered (/network)");
    
#if CONFIG_ENIP_SCANNER_ENABLE_MOTOMAN_SUPPORT
    httpd_uri_t motoman_position_uri = {
        .uri = "/motoman-position",
        .method = HTTP_GET,
        .handler = webui_motoman_position_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_position_uri);
    ESP_LOGI(TAG, "Motoman position page registered (/motoman-position)");

    httpd_uri_t motoman_alarm_uri = {
        .uri = "/motoman-alarms",
        .method = HTTP_GET,
        .handler = webui_motoman_alarm_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_alarm_uri);
    ESP_LOGI(TAG, "Motoman alarm page registered (/motoman-alarms)");

    httpd_uri_t motoman_status_uri = {
        .uri = "/motoman-status",
        .method = HTTP_GET,
        .handler = webui_motoman_status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_status_uri);
    ESP_LOGI(TAG, "Motoman status page registered (/motoman-status)");

    httpd_uri_t motoman_job_uri = {
        .uri = "/motoman-job",
        .method = HTTP_GET,
        .handler = webui_motoman_job_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_job_uri);
    ESP_LOGI(TAG, "Motoman job page registered (/motoman-job)");

    httpd_uri_t motoman_robot_position_uri = {
        .uri = "/motoman-robot-position",
        .method = HTTP_GET,
        .handler = webui_motoman_robot_position_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_robot_position_uri);
    ESP_LOGI(TAG, "Motoman robot position page registered (/motoman-robot-position)");

    httpd_uri_t motoman_position_dev_uri = {
        .uri = "/motoman-position-deviation",
        .method = HTTP_GET,
        .handler = webui_motoman_position_deviation_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_position_dev_uri);
    ESP_LOGI(TAG, "Motoman position deviation page registered (/motoman-position-deviation)");

    httpd_uri_t motoman_torque_uri = {
        .uri = "/motoman-torque",
        .method = HTTP_GET,
        .handler = webui_motoman_torque_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_torque_uri);
    ESP_LOGI(TAG, "Motoman torque page registered (/motoman-torque)");

    httpd_uri_t motoman_io_uri = {
        .uri = "/motoman-io",
        .method = HTTP_GET,
        .handler = webui_motoman_io_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_io_uri);
    ESP_LOGI(TAG, "Motoman IO page registered (/motoman-io)");

    httpd_uri_t motoman_register_uri = {
        .uri = "/motoman-register",
        .method = HTTP_GET,
        .handler = webui_motoman_register_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_register_uri);
    ESP_LOGI(TAG, "Motoman register page registered (/motoman-register)");

    httpd_uri_t motoman_var_b_uri = {
        .uri = "/motoman-variable-b",
        .method = HTTP_GET,
        .handler = webui_motoman_variable_b_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_var_b_uri);
    ESP_LOGI(TAG, "Motoman variable B page registered (/motoman-variable-b)");

    httpd_uri_t motoman_var_i_uri = {
        .uri = "/motoman-variable-i",
        .method = HTTP_GET,
        .handler = webui_motoman_variable_i_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_var_i_uri);
    ESP_LOGI(TAG, "Motoman variable I page registered (/motoman-variable-i)");

    httpd_uri_t motoman_var_d_uri = {
        .uri = "/motoman-variable-d",
        .method = HTTP_GET,
        .handler = webui_motoman_variable_d_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_var_d_uri);
    ESP_LOGI(TAG, "Motoman variable D page registered (/motoman-variable-d)");

    httpd_uri_t motoman_var_r_uri = {
        .uri = "/motoman-variable-r",
        .method = HTTP_GET,
        .handler = webui_motoman_variable_r_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_var_r_uri);
    ESP_LOGI(TAG, "Motoman variable R page registered (/motoman-variable-r)");

    httpd_uri_t motoman_var_s_uri = {
        .uri = "/motoman-variable-s",
        .method = HTTP_GET,
        .handler = webui_motoman_variable_s_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &motoman_var_s_uri);
    ESP_LOGI(TAG, "Motoman variable S page registered (/motoman-variable-s)");

#endif
    
    return ESP_OK;
}





