# Fast UTF8 detector

> Check utf8 using AVX2 and c++ source code from [simdjson](https://github.com/lemire/simdjson)

```
npm install fast-is-utf8
```

## Usage

``` js
const isUTF8 = require('fast-is-utf8');

const buf = Buffer.from([0xd0, 0x90]);
 
console.log(isUTF8(buf)); // => true
```

## Benchmark

```bash
$ node benchmark.js

fast-is-utf8 x 2,143,027 ops/sec ±0.37% (93 runs sampled)
is-utf8 x 660,326 ops/sec ±0.32% (94 runs sampled)
Fastest is fast-is-utf8
```

## License

MIT
