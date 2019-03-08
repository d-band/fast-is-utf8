{
  "target_defaults": {
    "cflags": ["-march=native", "-mavx"],
    "cxxflags": ["-march=native", "-mavx"],
    "xcode_settings": {
      "OTHER_CFLAGS": ["-march=native", "-mavx"],
      "OTHER_CXXFLAGS": ["-march=native", "-mavx"],
    }
  },
  "targets": [{
    "target_name": "is_utf8",
    "sources": [
      "is_utf8.cc"
    ]
  }]
}
