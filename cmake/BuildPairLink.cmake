# The signer requires an exact-size .dfpair section and seals the final PE bytes.
# MSVC incremental linking reserves section growth space (including on x86,
# where XFG's separate /INCREMENTAL:NO option is unavailable).
function(platformer_finalize_signed_pe target)
  if(MSVC)
    target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  endif()
endfunction()
