
set(BUILD_SHARED_LIBS OFF)

# Googletest
CPMAddPackage (
	NAME GoogleTest
	GITHUB_REPOSITORY google/googletest
	#VERSION release-1.10.0
	GIT_TAG 703bd9caab50b139428cea1aaff9974ebee5742e
)

enable_testing()

# Include the gtest library. gtest_SOURCE_DIR is available due to
# 'project(gtest)' above.
include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

##############
# Unit Tests
##############
add_executable(runUnitTests test_project1.cpp)


# Standard linking to gtest stuff.
target_link_libraries(runUnitTests gtest gtest_main)


# This is so you can do 'make test' to see all your tests run, instead of
# manually running the executable runUnitTests to see those specific tests.
add_test(NAME that-test-I-made COMMAND runUnitTests)
