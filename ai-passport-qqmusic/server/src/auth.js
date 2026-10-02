// QQ 音乐登录与数据封装层
// 登录（扫码 + MQTT 推送 + 设备指纹）复用社区维护、MIT 协议的
// @yakult-green-tea/qq-music-api（基于 Rain120/qq-music-api），以保证接口随官方升级可用；
// 本文件只做单用户会话管理与设备友好的数据归一化。搜索使用免签公开接口。
'use strict';

const path = require('path');
const fs = require('fs');
const crypto = require('crypto');
const https = require('https');
const { normalizePlaylists } = require('./playlist-normalize');

const DATA_DIR = path.join(__dirname, '..', 'data');
fs.mkdirSync(DATA_DIR, { recursive: true });

// --- 持久化路径与加密密钥（必须在 require 登录引擎前设置）---
process.env.QQ_AUTH_STATE_PATH = path.join(DATA_DIR, 'qq-device.json');
const SESSION_FILE = path.join(DATA_DIR, 'qq-session.json');
process.env.QQ_AUTH_SESSION_PATH = SESSION_FILE;
const SECRET_FILE = path.join(DATA_DIR, 'qq-secret.key');
let secret = '';
try { secret = fs.readFileSync(SECRET_FILE, 'utf8').trim(); } catch (_) {}
if (secret.length < 16) {
  secret = crypto.randomBytes(32).toString('hex');
  fs.writeFileSync(SECRET_FILE, secret, { mode: 0o600 });
}
process.env.QQ_SESSION_SECRET = secret;

const PKG_ROOT = path.dirname(require.resolve('@yakult-green-tea/qq-music-api/package.json'));
const qrNode = require(path.join(PKG_ROOT, 'dist', 'src', 'services', 'auth', 'qrLogin.node.js'));
const { configureAuthSessionPersistence } = require(
  path.join(PKG_ROOT, 'dist', 'src', 'services', 'auth', 'fileAuthSessionRepository.js'));
configureAuthSessionPersistence();
const svc = qrNode.qrLoginService;

const TOKEN_FILE = path.join(DATA_DIR, 'token.txt');
function loadToken() { try { return fs.readFileSync(TOKEN_FILE, 'utf8').trim(); } catch (_) { return ''; } }
function saveToken(t) { try { fs.writeFileSync(TOKEN_FILE, t || '', { mode: 0o600 }); } catch (_) {} }

const UA = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 ' +
           '(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36';

class MusicService {
  constructor() {
    this.token = loadToken();
    this.qrKey = '';
    this.qrImage = null;       // PNG Buffer
    this.qrCreatedAt = 0;
    this.profile = null;
  }

  // 申请新的扫码会话，返回 PNG 二维码
  async createQr() {
    this.qrKey = await svc.createSession('qq');
    const dataUrl = await svc.createQr(this.qrKey); // data:image/png;base64,...
    const b64 = dataUrl.split(',').pop();
    this.qrImage = Buffer.from(b64, 'base64');
    this.qrCreatedAt = Date.now();
    return this.qrImage;
  }

  // 状态：confirmed / scanned / waiting / expired
  // 内部自动懒创建二维码、自动续期过期码
  async qrState() {
    // 已登录优先校验
    if (this.token) {
      const p = await svc.getLoginStatus(this.token).catch(() => null);
      if (p && (p.musicid || p.str_musicid)) {
        this.profile = p;
        return 'confirmed';
      }
      this.token = ''; saveToken('');
    }
    if (!this.qrKey) await this.createQr();
    const r = await svc.checkQr(this.qrKey);
    if (r.code === 803 && r.cookie) {
      const m = /qqmusic_session=([^;]+)/.exec(r.cookie);
      if (m) {
        this.token = m[1];
        saveToken(this.token);
        this.profile = await svc.getLoginStatus(this.token).catch(() => null);
        return 'confirmed';
      }
    }
    if (r.code === 802) return 'scanned';
    if (r.code === 800) { await this.createQr().catch(() => {}); return 'waiting'; }
    return 'waiting';
  }

  async status() {
    if (!this.token) return { logged: false };
    const p = await svc.getLoginStatus(this.token).catch(() => null);
    if (!p || !(p.musicid || p.str_musicid)) return { logged: false };
    this.profile = p;
    return { logged: true, uin: p.musicid || p.str_musicid, nick: p.nickname || p.nick || '' };
  }

  async logout() {
    if (this.token) await svc.logout(this.token).catch(() => {});
    this.token = ''; this.profile = null; saveToken('');
  }

  async playlists() {
    const raw = await svc.getUserPlaylists(this.token);
    return normalizePlaylists(raw);
  }

  async playlistSongs(id) {
    const out = [];
    const limit = 100;
    for (let offset = 0; offset < 1000; offset += limit) {
      const raw = await svc.getOwnedPlaylistSongs(this.token, {
        disstid: Number(id) || 0, dirid: Number(id) || 0, offset, limit,
      });
      const songs = (raw && (raw.songlist || raw.songs ||
        (raw.cdlist && raw.cdlist[0] && (raw.cdlist[0].songlist || raw.cdlist[0].songs)))) || [];
      for (const s of songs) out.push(this._norm(s));
      const total = Number(raw && (raw.total_song_num ?? raw.total ?? raw.song_total)) || 0;
      if (songs.length < limit || (total && out.length >= total)) break;
    }
    return out.filter((s) => s.mid);
  }

  // 免签公开搜索（无需登录）
  search(keyword) {
    const url = 'https://c.y.qq.com/soso/fcgi-bin/client_search_cp?' +
      `w=${encodeURIComponent(keyword)}&format=json&n=30&p=1&cr=1&aggr=1&flag_qc=1&new_json=1`;
    return this._get(url, { Referer: 'https://y.qq.com/' }).then((text) => {
      const json = JSON.parse(text.replace(/^[^{]*/, ''));
      const list = (((json.data || {}).song || {}).list) || [];
      return list.map((s) => this._norm(s)).filter((s) => s.mid);
    });
  }

  // 返回 128kbps MP3 直链；后端会再转成 16kHz/24kbps 低负载流。
  async playUrl(mid) {
    const map = await svc.getMusicPlay(this.token, mid, '128');
    const item = map && map[mid];
    if (!item || !item.url) return null;
    return item.url;
  }

  _norm(s) {
    const singer = (s.singer || s.ar || []).map((x) => x.name).filter(Boolean).join(' / ');
    return {
      mid: s.songmid || s.mid || '',
      name: s.songname || s.name || s.title || '',
      singer,
      album: (s.album && s.album.name) || s.albumname || '',
      albummid: (s.album && (s.album.mid || s.album.pmid)) || s.albummid || s.strMediaMid || '',
      duration: (() => { const d = Number(s.interval || s.dt || 0); return d > 10000 ? Math.round(d / 1000) : d; })(),
    };
  }


  async lyric(mid) {
    const q = new URLSearchParams({
      songmid: mid,
      format: 'json',
      nobase64: '1',
      outCharset: 'utf-8',
    });
    const text = await this._get('https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?' + q, {
      Referer: 'https://y.qq.com/',
    }).catch(() => '');
    let raw = '';
    try { raw = JSON.parse(text.replace(/^[^{]*/, '')).lyric || ''; } catch (_) {}
    const lines = [];
    for (const line of String(raw || '').split(/\r?\n/)) {
      const m = /^\[(\d+):(\d+(?:\.\d+)?)\](.*)$/.exec(line.trim());
      if (!m) continue;
      const sec = Number(m[1]) * 60 + Number(m[2]);
      const lyricText = m[3].replace(/<[^>]+>/g, '').trim();
      if (!lyricText || /^(作词|作曲|编曲|制作人|出品|发行|OP|SP|ISRC|词：|曲：)/.test(lyricText)) continue;
      lines.push({ t: Math.max(0, Math.round(sec)), text: lyricText.slice(0, 48) });
      if (lines.length >= 80) break;
    }
    return lines;
  }

  albumCoverUrl(albummid) {
    if (!albummid) return '';
    return `https://y.gtimg.cn/music/photo_new/T002R300x300M000${encodeURIComponent(albummid)}.jpg`;
  }

  _get(urlStr, headers = {}) {
    return new Promise((resolve, reject) => {
      const u = new URL(urlStr);
      https.get({
        hostname: u.hostname, path: u.pathname + u.search,
        headers: Object.assign({ 'User-Agent': UA, 'Accept': '*/*' }, headers),
      }, (res) => {
        const chunks = [];
        res.on('data', (c) => chunks.push(c));
        res.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
      }).on('error', reject);
    });
  }
}

module.exports = { MusicService, UA };
