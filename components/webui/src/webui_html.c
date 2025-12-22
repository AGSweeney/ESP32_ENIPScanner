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

// Main page HTML
static const char index_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>EtherNet/IP Scanner</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px}"
".n a{display:inline-block;margin-right:15px;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px}"
".n a:hover{background:#45a049}"
".n span.active{background:#9e9e9e;opacity:0.6;display:inline-block;margin-right:15px;padding:8px 15px;color:#fff;border-radius:4px}"
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
"    navHtml += '<a href=\"/network\">Network</a>';"
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
    return httpd_resp_send(req, index_page, html_len);
}

#if CONFIG_ENIP_SCANNER_ENABLE_TAG_SUPPORT
// Tag test page HTML
static const char tags_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Read Tag</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px}"
".n a{display:inline-block;margin-right:15px;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px}"
".n a:hover{background:#45a049}"
".n span.active{background:#9e9e9e;opacity:0.6;display:inline-block;margin-right:15px;padding:8px 15px;color:#fff;border-radius:4px}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input,select{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"</style></head><body>"
"<div class=\"c\"><h1>Read Tag</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a><span class=\"active\">Read Tag</span><a href=\"/write-tag\">Write Tag</a><a href=\"/network\">Network</a></div>"
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
"if(d.value_bool!==undefined){h+='<div style=\"margin:5px 0;padding:5px;border-bottom:1px solid #eee\"><strong>Value (BOOL):</strong> '+d.value_bool+'</div>';}"
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
"</script></body></html>";

// Write tag page HTML
static const char write_tags_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Write Tag</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px}"
".n a{display:inline-block;margin-right:15px;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px}"
".n a:hover{background:#45a049}"
".n span.active{background:#9e9e9e;opacity:0.6;display:inline-block;margin-right:15px;padding:8px 15px;color:#fff;border-radius:4px}"
"label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}"
"input,select{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
"button{background:#4CAF50;color:#fff;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px}"
"button:hover{background:#45a049}"
".e{color:#f44336;background:#ffebee;padding:10px;border-radius:4px;margin:10px 0}"
".s{color:#4CAF50;background:#e8f5e9;padding:10px;border-radius:4px;margin:10px 0}"
".i{color:#2196F3;background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0}"
"</style></head><body>"
"<div class=\"c\"><h1>Write Tag</h1>"
"<div class=\"n\"><a href=\"/\">Assembly I/O</a><a href=\"/tags\">Read Tag</a><span class=\"active\">Write Tag</span><a href=\"/network\">Network</a></div>"
"<label>IP Address:</label><input type=\"text\" id=\"ip\" placeholder=\"192.168.1.100\">"
"<label>Tag Path:</label><input type=\"text\" id=\"tag\" placeholder=\"MyTag\">"
"<label>Data Type:</label><select id=\"type\"><option value=\"193\">BOOL</option><option value=\"194\">SINT</option><option value=\"195\">INT</option><option value=\"196\" selected>DINT</option><option value=\"202\">REAL</option></select>"
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
"else{r.innerHTML='<div class=\"e\">Unsupported type</div>';return;}"
"}catch(e){r.innerHTML='<div class=\"e\">Invalid value</div>';return;}"
"r.innerHTML='<div class=\"i\">Writing...</div>';"
"fetch('/api/scanner/write-tag',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ip_address:ip,tag_path:tag,cip_data_type:type,data:d,timeout_ms:to})})"
".then(function(x){return x.json();})"
".then(function(x){r.innerHTML=x.success?'<div class=\"s\">Success!</div>':'<div class=\"e\">Failed: '+(x.error||'Unknown')+'</div>';})"
".catch(function(e){r.innerHTML='<div class=\"e\">Error: '+e.message+'</div>';});"
"}"
"window.writeTag=w;"
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

// Network configuration page HTML
static const char network_config_page[] =
"<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>Network Config</title>"
"<style>"
"body{font-family:Arial;margin:20px;background:#f5f5f5}"
".c{max-width:800px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}"
"h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}"
".n{margin-bottom:20px;padding:10px;background:#f9f9f9;border-radius:5px}"
".n a{display:inline-block;margin-right:15px;padding:8px 15px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:4px}"
".n a:hover{background:#45a049}"
".n span.active{background:#9e9e9e;opacity:0.6;display:inline-block;margin-right:15px;padding:8px 15px;color:#fff;border-radius:4px}"
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
"h+='<span class=\"active\">Network</span>';n.innerHTML=h;}loadConfig();});"
"</script></body></html>";

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
    
    httpd_uri_t network_config_uri = {
        .uri = "/network",
        .method = HTTP_GET,
        .handler = webui_network_config_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &network_config_uri);
    ESP_LOGI(TAG, "Network config page registered (/network)");
    
    return ESP_OK;
}
