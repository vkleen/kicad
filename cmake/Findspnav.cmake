find_path(SPNAV_INCLUDE_DIR spnav.h)
find_library(SPNAV_LIBRARY libspnav.a)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(spnav
  FOUND_VAR
    SPNAV_FOUND
  REQUIRED_VARS
    SPNAV_INCLUDE_DIR
    SPNAV_LIBRARY
)

mark_as_advanced(
  SPNAV_INCLUDE_DIR
  SPNAV_LIBRARY
)
