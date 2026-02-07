file(REMOVE_RECURSE
  "libwebp.pdb"
  "libwebp.so"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/webp.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
