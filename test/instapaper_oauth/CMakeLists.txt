add_executable(InstapaperOAuthTest
  InstapaperOAuthTest.cpp
  ${REPO_ROOT}/lib/InstapaperSync/OAuth1.cpp
)

target_include_directories(InstapaperOAuthTest PRIVATE
  ${REPO_ROOT}/lib/InstapaperSync
)

target_link_libraries(InstapaperOAuthTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(InstapaperOAuthTest)
