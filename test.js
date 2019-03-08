const fs = require('fs');
const tape = require('tape');
const is_utf8 = require('is-utf8');
const fast_is_utf8 = require('./');

tape('is utf8', (t) => {
  ['ansi', 'koi8r', 'utf8', 'win1251'].forEach((f) => {
    const buf = fs.readFileSync(`./texts/${f}.txt`);
    t.equal(is_utf8(buf), fast_is_utf8(buf));
  });
  t.end();
});
