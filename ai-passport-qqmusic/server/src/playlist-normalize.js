'use strict';

function normalizePlaylists(raw) {
  const rows = Array.isArray(raw && raw.v_playlist) ? raw.v_playlist : [];
  return rows.map((p) => ({
    id: String(p.tid ?? p.dissid ?? p.id ?? p.dirId ?? p.dirid ?? ''),
    name: p.dirName || p.title || p.name || '未命名歌单',
    count: Number(p.songNum ?? p.song_num ?? p.songnum ?? p.total_song_num ?? 0) || 0,
    cover: String(p.picUrl || p.cover || p.logo || '').replace(/^http:/, 'https:'),
  })).filter((p) => p.id && p.count > 0);
}

module.exports = { normalizePlaylists };
