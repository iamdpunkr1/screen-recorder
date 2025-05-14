{
  "targets": [
    {
      "target_name": "screen_recorder",
      "sources": ["src/screen_recorder.cc"],
      "include_dirs": ["<!@(node -p \"require('node-addon-api').include\")"],
      "dependencies": ["<!(node -p \"require('node-addon-api').gyp\")"],
      "cflags!": ["-fno-exceptions"],
      "cflags_cc!": ["-fno-exceptions"],
      "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"],
      "conditions": [
        ["OS=='win'", {
          "libraries": ["-lgdi32"]
        }],
        ["OS=='mac'", {
          "libraries": ["-framework ApplicationServices"]
        }],
        ["OS=='linux'", {
          "libraries": ["-lX11"]
        }]
      ]
    }
  ]
}