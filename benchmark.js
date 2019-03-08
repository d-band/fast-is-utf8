const fs = require('fs');
const Benchmark = require('benchmark');
const fast_is_utf8 = require('./');
const is_utf8 = require('is-utf8');

const list = ['ansi', 'koi8r', 'utf8', 'win1251'].map(f => {
  return fs.readFileSync(`./texts/${f}.txt`);
});

const suite = new Benchmark.Suite();

suite
  .add('fast-is-utf8', () => {
    list.forEach(txt => fast_is_utf8(txt));
  })
  .add('is-utf8', () => {
    list.forEach(txt => is_utf8(txt));
  })
  .on('cycle', (event) => {
    console.log(String(event.target));
  })
  .on('complete', function () {
    console.log('Fastest is ' + this.filter('fastest').map('name'));
  })
  .run({ async: false });