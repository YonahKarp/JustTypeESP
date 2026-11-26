#include "content_browser.h"
#include "content.h"
#include "cJSON.h"
#include <emscripten.h>
#include <cstdio>
#include <sys/stat.h>

// MEMFS is the browser's writable storage, persisted by the existing IDBFS
// adapter. Approximate the physical filesystem budget, including cluster cost.
EM_JS(double, leaf_browser_content_free, (), {
  Module.leafContentFreeSpace = () => {
  let used = 128 * 1024;
  const visit = path => {
    for (const name of FS.readdir(path)) {
      if (name === '.' || name === '..') continue;
      const p = path + '/' + name;
      const st = FS.stat(p);
      if (FS.isDir(st.mode)) { used += 4096; visit(p); }
      else used += Math.ceil(st.size / 4096) * 4096;
    }
  };
  if (FS.analyzePath('/spiffs').exists) visit('/spiffs');
  return Math.max(0, 0xbf0000 - used);
  };
  return Module.leafContentFreeSpace();
});

EM_JS(void, clear_downloads, (), {
  const root = '/spiffs/leaf_content/staging/browser-download';
  if (FS.analyzePath(root).exists) {
    for (const name of FS.readdir(root))
      if (/^\d+\.part$/.test(name)) FS.unlink(root + '/' + name);
    try { FS.rmdir(root); } catch (_) {}
  }
});

EM_JS(void, begin_downloads, (const char *json), {
  const jobs = JSON.parse(UTF8ToString(json));
  const root = '/spiffs/leaf_content/staging/browser-download';
  const state = Module.leafContentDownload = {
    status: 1, progress: -1, message: 'Starting content download',
    cancelled: false, controller: null
  };
  const start = Date.now();
  const timeout = (promise, ms, controller) => new Promise((resolve, reject) => {
    const timer = setTimeout(() => { controller.abort(); reject(new Error('Content download timed out')); }, ms);
    promise.then(value => { clearTimeout(timer); resolve(value); }, error => { clearTimeout(timer); reject(error); });
  });
  const check = () => {
    if (state.cancelled) throw new Error('Content download cancelled');
    if (Date.now() - start > 240000) throw new Error('Content download time limit reached; retry');
  };
  const run = async () => {
    if (!globalThis.crypto?.subtle) throw new Error('Content downloads require HTTPS or localhost');
    if (navigator.storage?.estimate) {
      const estimate = await navigator.storage.estimate();
      const needed = jobs.reduce((total, job) => total + job.maximum, 0) + 1024 * 1024;
      if (Number.isFinite(estimate.quota) && Number.isFinite(estimate.usage) && estimate.quota - estimate.usage < needed)
        throw new Error('Browser storage quota is too low for this download');
    }
    FS.mkdirTree(root);
    for (let i = 0; i < jobs.length; ++i) {
      const job = jobs[i];
      const file = root + '/' + i + '.part';
      let complete = false;
      for (let attempt = 0; attempt < 3 && !complete; ++attempt) {
        check();
        state.message = 'Downloading content (' + (i + 1) + '/' + jobs.length + ')';
        let retryable = true, stream = null, reader = null;
        const controller = state.controller = new AbortController();
        try {
          const url = new URL('/api/leaf/content/v1/' + job.relative, location.href);
          const response = await timeout(fetch(url, {
            signal: controller.signal, credentials: 'omit', redirect: 'error',
            cache: job.relative === 'catalog' ? 'no-cache' : 'default'
          }), 10000, controller);
          retryable = response.status === 408 || response.status === 429 || response.status >= 500;
          if (response.status !== 200) throw new Error('Content server returned HTTP ' + response.status);
          retryable = true;
          const lengthHeader = response.headers.get('content-length');
          const length = lengthHeader === null ? null : Number(lengthHeader);
          // Browser Fetch decompresses gzip/Brotli; Content-Length may describe
          // compressed bytes. The manifest/hash always describe decoded bytes.
          const identity = !response.headers.get('content-encoding');
          if (identity && length !== null && (!Number.isSafeInteger(length) || length < 0 || length > job.maximum)) {
            retryable = false; throw new Error('Content size exceeds package limit');
          }
          if (!response.body) throw new Error('Content response has no body');
          stream = FS.open(file, 'w'); reader = response.body.getReader();
          let count = 0;
          while (true) {
            check();
            const {done, value} = await timeout(reader.read(), 10000, controller);
            if (done) break;
            if (count + value.length > job.maximum) { retryable = false; throw new Error('Content size exceeds package limit'); }
            if (Module.leafContentFreeSpace() < 1024 * 1024 + value.length + 4096) {
              retryable = false; throw new Error('Not enough storage; 1 MiB is reserved for documents');
            }
            for (let offset = 0; offset < value.length; offset += 2048) {
              const block = value.subarray(offset, offset + 2048);
              FS.write(stream, block, 0, block.length, count); count += block.length;
            }
            state.progress = job.sha256 ? Math.min(99, Math.floor((i + count / job.maximum) * 100 / jobs.length)) : -1;
            // Fetch may already have all chunks buffered; explicitly give input,
            // timers and rendering a turn between chunks.
            await new Promise(resolve => setTimeout(resolve, 0));
          }
          FS.close(stream); stream = null;
          retryable = false;
          if ((job.sha256 && count !== job.maximum) || (identity && length !== null && count !== length))
            throw new Error('Content length verification failed');
          if (job.sha256) {
            state.message = 'Verifying content';
            const bytes = FS.readFile(file);
            const digest = await crypto.subtle.digest('SHA-256', bytes);
            const hex = Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
            if (hex !== job.sha256) throw new Error('Content hash verification failed');
          }
          check(); complete = true;
        } catch (error) {
          if (stream) FS.close(stream);
          if (reader) { try { await reader.cancel(); } catch (_) {} }
          controller.abort();
          if (FS.analyzePath(file).exists) FS.unlink(file);
          if (state.cancelled || !retryable || attempt === 2) throw error;
          state.message = 'Retrying interrupted content download';
          await new Promise(resolve => setTimeout(resolve, 200 << attempt));
        }
      }
    }
    state.progress = 100; state.status = 2;
  };
  run().catch(error => {
    state.message = state.cancelled ? 'Content download cancelled; installed content retained' : error.message || 'Content download failed';
    state.status = 3;
  });
});

EM_JS(int, poll_downloads, (int cancelled), {
  const state = Module.leafContentDownload;
  if (!state) return 0;
  if (cancelled) { state.cancelled = true; state.controller?.abort(); }
  return state.status;
});
EM_JS(int, download_percent, (), { return Module.leafContentDownload?.progress ?? -1; });
EM_JS(void, download_message, (char *buffer, int size), {
  stringToUTF8(Module.leafContentDownload?.message || 'Content download failed', buffer, size);
});

namespace browser_content {
namespace {
bool pending = false;
std::vector<content::DownloadRequest> requests;
bool consume(const std::string &relative, const std::string &destination,
             uint32_t maximum, const std::string &sha256) {
  for (size_t i=0; i<requests.size(); ++i) {
    const auto &job=requests[i];
    if (job.relative!=relative || job.maximum!=maximum || job.sha256!=sha256) continue;
    const std::string staged="/spiffs/leaf_content/staging/browser-download/"+std::to_string(i)+".part";
    struct stat st{};
    if(stat(staged.c_str(),&st)!=0 || st.st_size<0 || uint64_t(st.st_size)>maximum || (!sha256.empty() && uint64_t(st.st_size)!=maximum)) return false;
    return std::rename(staged.c_str(),destination.c_str())==0;
  }
  return false;
}
}
void init() { clear_downloads(); }
void request() {
  if(pending)return;
  if(!content::needs_network()){content::process(nullptr);return;}
  std::string error;
  if(!content::download_plan(requests,error)){content::network_error(error);return;}
  if(requests.empty()){content::process(consume,true);return;}
  auto *array=cJSON_CreateArray();
  for(const auto &job:requests){
    auto *item=cJSON_CreateObject();
    cJSON_AddStringToObject(item,"relative",job.relative.c_str());
    cJSON_AddStringToObject(item,"sha256",job.sha256.c_str());
    cJSON_AddNumberToObject(item,"maximum",job.maximum);
    cJSON_AddItemToArray(array,item);
  }
  char *json=cJSON_PrintUnformatted(array);cJSON_Delete(array);
  if(!json){content::network_error("Could not allocate content request");return;}
  clear_downloads();pending=true;begin_downloads(json);cJSON_free(json);
}
void tick() {
  if(!pending)return;
  const int status=poll_downloads(content::cancelled());
  char message[192]{};download_message(message,sizeof message);
  if(status==1){content::download_progress(download_percent(),message);return;}
  pending=false;
  if(status==2 && !content::cancelled())content::process(consume,true);
  else content::network_error(message);
  clear_downloads();requests.clear();
}
}
