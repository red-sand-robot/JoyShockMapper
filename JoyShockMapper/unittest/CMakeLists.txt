
set(BUILD_SHARED_LIBS OFF)

# Googletest
CPMAddPackage (
	NAME GoogleTest
	GITHUB_REPOSITORY google/googletest
	#VERSION release-1.10.0
	GIT_TAG 703bd9caab50b139428cea1aaff9974ebee5742e
)

enable_testing()


##############
# Unit Tests
##############
add_executable(runUnitTests testRegression.cpp 
    ../src/main.cpp
    ../src/operators.cpp
    ../src/CmdRegistry.cpp
    ../src/quatMaths.cpp
    ../src/ButtonHelp.cpp
    ../src/win32/PlatformDefinitions.cpp
	mockGamepad.cpp
	mockJslWrapper.hpp
	mockWhitelister.hpp
	mockInputHelpers.hpp
)

# Include the gtest library. gtest_SOURCE_DIR is available due to
# 'project(gtest)' above.
include_directories( ../include
	${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR}
	${gmock_SOURCE_DIR}/include ${gmock_SOURCE_DIR}
	"${PROJECT_BINARY_DIR}/${PROJECT_NAME}/include"
)

# Standard linking to gtest stuff.
target_link_libraries(runUnitTests gtest gmock gmock_main magic_enum Platform::Dependencies)


# This is so you can do 'make test' to see all your tests run, instead of
# manually running the executable runUnitTests to see those specific tests.
add_test(NAME Commands.RECONNECT_CONTROLLERS COMMAND runUnitTests)
