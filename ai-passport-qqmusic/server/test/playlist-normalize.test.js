'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { normalizePlaylists } = require('../src/playlist-normalize');

test('keeps owned playlists and favorites in upstream order', () => {
  const list = normalizePlaylists({ v_playlist: [
    { tid: 11, dirId: 201, dirName: '我喜欢', songNum: 273, picUrl: 'http://cover/a' },
    { tid: 12, dirId: 15, dirName: '自建歌单', songNum: 96 },
    { tid: 13, name: '收藏歌单', songnum: 64, logo: 'http://cover/b' },
  ] });
  assert.deepEqual(list.map(({ id, name, count }) => ({ id, name, count })), [
    { id: '11', name: '我喜欢', count: 273 },
    { id: '12', name: '自建歌单', count: 96 },
    { id: '13', name: '收藏歌单', count: 64 },
  ]);
  assert.equal(list[0].cover, 'https://cover/a');
  assert.equal(list[2].cover, 'https://cover/b');
});
