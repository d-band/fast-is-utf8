const binding = require('node-gyp-build')(__dirname);

module.exports = binding.is_utf8;
