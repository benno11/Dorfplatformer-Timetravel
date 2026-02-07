file(REMOVE_RECURSE
  "libwebpdecoder.pdb"
  "libwebpdecoder.so"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/webpdecoder.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
