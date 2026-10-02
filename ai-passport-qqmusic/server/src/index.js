// AI Passport QQ 音乐播放器 —— 设备后端服务
// 为 AI Passport 固件提供：扫码登录状态、我的歌单、歌曲列表、MP3 音频流代理。
// 同时提供网页扫码页（电脑/手机浏览器打开，用 QQ 音乐 App 扫码）。
'use strict';

const http = require('http');
const https = require('https');
const fs = require('fs');
const path = require('path');
const os = require('os');
const { spawn } = require('child_process');
const { MusicService, UA } = require('./auth');

const PORT = process.env.PORT ? Number(process.env.PORT) : 3200;
const PUBLIC_DIR = path.join(__dirname, '..', 'public');
const music = new MusicService();

function json(res, code, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(code, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body),
    'Cache-Control': 'no-store',
  });
  res.end(body);
}

// 音频流代理：服务端转成 AI Passport 更容易解码的低码率 MP3。
// ESP32-C3 内存很紧，直通 QQ CDN 的高码率/大帧音频会导致缓冲满但解码不起。
function proxyAudio(res, urlStr) {
  const headerText = `User-Agent: ${UA}\r\nReferer: https://y.qq.com/\r\n`;
  const args = [
    '-hide_banner', '-loglevel', 'error',
    '-headers', headerText,
    '-reconnect', '1', '-reconnect_streamed', '1', '-reconnect_delay_max', '2',
    '-i', urlStr,
    '-vn', '-map', '0:a:0',
    '-ac', '1',          // 单声道
    '-ar', '16000',      // 最低可用采样率，显著降低解码负载
    '-b:a', '24k',       // 低码率，减少 Wi-Fi 与环形缓冲压力
    '-codec:a', 'libmp3lame',
    '-f', 'mp3',
    'pipe:1',
  ];
  const ffmpegBin = process.env.FFMPEG_BIN || 'ffmpeg';
  const ff = spawn(ffmpegBin, args, { stdio: ['ignore', 'pipe', 'pipe'] });
  let wroteHead = false;
  let stderr = '';

  const fail = (message) => {
    if (!wroteHead && !res.headersSent) {
      json(res, 502, { error: message });
    } else {
      try { res.destroy(); } catch (_) {}
    }
  };

  ff.stdout.once('data', (first) => {
    wroteHead = true;
    res.writeHead(200, {
      'Content-Type': 'audio/mpeg',
      'Cache-Control': 'no-store',
      'X-Audio-Mode': 'mp3-16k-mono-24k',
    });
    res.write(first);
    ff.stdout.pipe(res);
  });
  ff.stderr.on('data', (d) => {
    stderr += d.toString();
    if (stderr.length > 1200) stderr = stderr.slice(-1200);
  });
  ff.on('error', (err) => fail('ffmpeg 启动失败: ' + err.message));
  ff.on('close', (code) => {
    if (!wroteHead) fail('音频转码失败: ' + (stderr.trim() || code));
    else if (!res.destroyed) res.end();
  });
  res.on('close', () => {
    if (!ff.killed) ff.kill('SIGKILL');
  });
}


function proxyCoverRaw(res, urlStr) {
  const headerText = `User-Agent: ${UA}\r\nReferer: https://y.qq.com/\r\n`;
  const args = [
    '-hide_banner', '-loglevel', 'error',
    '-headers', headerText,
    '-i', urlStr,
    '-vf', 'scale=64:64:force_original_aspect_ratio=increase,crop=64:64,format=rgb565le',
    '-frames:v', '1',
    '-f', 'rawvideo',
    'pipe:1',
  ];
  const ffmpegBin = process.env.FFMPEG_BIN || 'ffmpeg';
  const ff = spawn(ffmpegBin, args, { stdio: ['ignore', 'pipe', 'pipe'] });
  const chunks = [];
  let stderr = '';
  ff.stdout.on('data', (d) => chunks.push(d));
  ff.stderr.on('data', (d) => {
    stderr += d.toString();
    if (stderr.length > 1200) stderr = stderr.slice(-1200);
  });
  ff.on('error', (err) => json(res, 502, { error: 'ffmpeg 启动失败: ' + err.message }));
  ff.on('close', (code) => {
    const body = Buffer.concat(chunks);
    if (body.length !== 64 * 64 * 2) {
      return json(res, 502, { error: '封面转换失败: ' + (stderr.trim() || code || body.length) });
    }
    res.writeHead(200, {
      'Content-Type': 'application/octet-stream',
      'Content-Length': body.length,
      'Cache-Control': 'public, max-age=86400',
      'X-Cover-Format': 'rgb565le-64x64',
    });
    res.end(body);
  });
  res.on('close', () => {
    if (!ff.killed) ff.kill('SIGKILL');
  });
}


function proxyPcm(res, urlStr) {
  const headerText = `User-Agent: ${UA}\r\nReferer: https://y.qq.com/\r\n`;
  const args = [
    '-hide_banner', '-loglevel', 'error',
    '-headers', headerText,
    '-reconnect', '1', '-reconnect_streamed', '1', '-reconnect_delay_max', '2',
    '-i', urlStr,
    '-vn', '-map', '0:a:0',
    '-ac', '1',
    '-ar', '16000',
    '-f', 's16le',
    'pipe:1',
  ];
  const ffmpegBin = process.env.FFMPEG_BIN || 'ffmpeg';
  const ff = spawn(ffmpegBin, args, { stdio: ['ignore', 'pipe', 'pipe'] });
  let wroteHead = false;
  let stderr = '';
  const fail = (message) => {
    if (!wroteHead && !res.headersSent) json(res, 502, { error: message });
    else { try { res.destroy(); } catch (_) {} }
  };
  ff.stdout.once('data', (first) => {
    wroteHead = true;
    res.writeHead(200, {
      'Content-Type': 'application/octet-stream',
      'Cache-Control': 'no-store',
      'X-Audio-Mode': 'pcm-s16le-16k-mono',
    });
    res.write(first);
    ff.stdout.pipe(res);
  });
  ff.stderr.on('data', (d) => {
    stderr += d.toString();
    if (stderr.length > 1200) stderr = stderr.slice(-1200);
  });
  ff.on('error', (err) => fail('ffmpeg 启动失败: ' + err.message));
  ff.on('close', (code) => {
    if (!wroteHead) fail('PCM 转码失败: ' + (stderr.trim() || code));
    else if (!res.destroyed) res.end();
  });
  res.on('close', () => {
    if (!ff.killed) ff.kill('SIGKILL');
  });
}

const server = http.createServer(async (req, res) => {
  const u = new URL(req.url, `http://${req.headers.host}`);
  const p = u.pathname;
  try {
    if (p === '/' || p === '/index.html') {
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
      return res.end(fs.readFileSync(path.join(PUBLIC_DIR, 'index.html')));
    }

    if (p === '/api/status') {
      return json(res, 200, await music.status());
    }

    // 二维码图片（PNG）
    if (p === '/api/qrcode/image') {
      const png = music.qrImage || await music.createQr();
      res.writeHead(200, { 'Content-Type': 'image/png', 'Cache-Control': 'no-store' });
      return res.end(png);
    }
    if (p === '/api/qrcode/state') return json(res, 200, { state: await music.qrState() });
    if (p === '/api/qrcode/refresh' && req.method === 'POST') {
      await music.createQr();
      return json(res, 200, { ok: true });
    }
    if (p === '/api/logout' && req.method === 'POST') {
      await music.logout();
      return json(res, 200, { ok: true });
    }

    const st = await music.status();
    if (p === '/api/playlists') {
      if (!st.logged) return json(res, 401, { error: '未登录' });
      return json(res, 200, { list: await music.playlists() });
    }
    if (p === '/api/playlist') {
      if (!st.logged) return json(res, 401, { error: '未登录' });
      const id = u.searchParams.get('id');
      if (!id) return json(res, 400, { error: '缺少 id' });
      const _songs = await music.playlistSongs(id);
      return json(res, 200, { list: _songs.slice(0, 60) });
    }
    if (p === '/api/search') {
      const q = u.searchParams.get('q') || '';
      if (!q) return json(res, 400, { error: '缺少 q' });
      return json(res, 200, { list: await music.search(q) });
    }

    if (p === '/api/lyric') {
      const mid = u.searchParams.get('mid');
      if (!mid) return json(res, 400, { error: '缺少 mid' });
      if (!st.logged) return json(res, 401, { error: '未登录' });
      return json(res, 200, { lines: await music.lyric(mid) });
    }

    if (p === '/api/cover.raw') {
      const albummid = u.searchParams.get('albummid') || '';
      const cover = music.albumCoverUrl(albummid);
      if (!cover) return json(res, 404, { error: '无专辑图' });
      return proxyCoverRaw(res, cover);
    }
    if (p === '/api/cover') {
      const albummid = u.searchParams.get('albummid') || '';
      const cover = music.albumCoverUrl(albummid);
      if (!cover) return json(res, 404, { error: '无专辑图' });
      https.get(cover, { headers: { 'User-Agent': UA, 'Referer': 'https://y.qq.com/' } }, (up) => {
        if ((up.statusCode || 500) >= 400) return json(res, 404, { error: '专辑图获取失败' });
        res.writeHead(200, { 'Content-Type': up.headers['content-type'] || 'image/jpeg', 'Cache-Control': 'public, max-age=86400' });
        up.pipe(res);
      }).on('error', () => json(res, 502, { error: '专辑图代理失败' }));
      return;
    }

    if (p === '/api/pcm') {
      const mid = u.searchParams.get('mid');
      if (!mid) return json(res, 400, { error: '缺少 mid' });
      if (!st.logged) return json(res, 401, { error: '未登录' });
      const url = await music.playUrl(mid);
      if (!url) return json(res, 404, { error: '该歌曲暂无可播放源（VIP 专享/无版权）' });
      return proxyPcm(res, url);
    }
    if (p === '/api/stream') {
      const mid = u.searchParams.get('mid');
      if (!mid) return json(res, 400, { error: '缺少 mid' });
      if (!st.logged) return json(res, 401, { error: '未登录' });
      const url = await music.playUrl(mid);
      if (!url) return json(res, 404, { error: '该歌曲暂无可播放源（VIP 专享/无版权）' });
      return proxyAudio(res, url);
    }
    json(res, 404, { error: 'Not Found' });
  } catch (err) {
    console.error('[server]', err);
    json(res, 500, { error: err.message });
  }
});

function lanAddresses() {
  const out = [];
  for (const list of Object.values(os.networkInterfaces())) {
    for (const it of list || []) {
      if (it.family === 'IPv4' && !it.internal) out.push(it.address);
    }
  }
  return out;
}

server.listen(PORT, '0.0.0.0', () => {
  console.log('');
  console.log('  QQ 音乐播放器后端已启动');
  console.log('  本机访问:   http://localhost:' + PORT);
  for (const ip of lanAddresses()) {
    console.log('  设备/手机:  http://' + ip + ':' + PORT + '   (AI Passport 填这个地址)');
  }
  console.log('  登录状态:   ' + (music.token ? '已有登录凭证（启动后自动校验）' : '未登录，请打开网页扫码'));
  console.log('');
});
