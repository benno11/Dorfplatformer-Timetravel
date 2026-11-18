# Recent SDL/vcpkg configs report codec support independently of library presence.
# Finding SDL_image/mixer alone does not guarantee the game's files can decode.
# Older dependency packages may not export these capability variables.
foreach(_image_codec IN ITEMS PNG JPG)
  if(DEFINED SDLIMAGE_${_image_codec} AND NOT SDLIMAGE_${_image_codec})
    message(FATAL_ERROR "SDL3_image lacks required ${_image_codec} decoding. Install sdl3-image[png,jpeg] or enable the corresponding SDLIMAGE codec options.")
  endif()
endforeach()
foreach(_audio_codec IN ITEMS MP3 WAVE)
  if(DEFINED SDLMIXER_${_audio_codec} AND NOT SDLMIXER_${_audio_codec})
    message(FATAL_ERROR "SDL3_mixer lacks required ${_audio_codec} decoding. Install sdl3-mixer[mpg123] and enable SDLMIXER_WAVE.")
  endif()
endforeach()
