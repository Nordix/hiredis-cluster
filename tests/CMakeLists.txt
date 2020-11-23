if(ENABLE_SSL)
  # Generate SSL certs and keys when needed
  set(SSL_CONFIGS ca.crt ca.key ca.txt redis.crt redis.key client.crt client.key)
  add_custom_command(
    OUTPUT ${SSL_CONFIGS}
    COMMAND openssl genrsa -out ca.key 4096
    COMMAND openssl req -x509 -new -nodes -sha256 -key ca.key -days 3650 -subj '/CN=Redis Test CA' -out ca.crt
    COMMAND openssl genrsa -out redis.key 2048
    COMMAND openssl req -new -sha256 -key redis.key -subj '/CN=Redis Server Test Cert' | openssl x509 -req -sha256 -CA ca.crt -CAkey ca.key -CAserial ca.txt -CAcreateserial -days 365 -out redis.crt
    COMMAND openssl genrsa -out client.key 2048
    COMMAND openssl req -new -sha256 -key client.key -subj '/CN=Redis Client Test Cert' | openssl x509 -req -sha256 -CA ca.crt -CAkey ca.key -CAserial ca.txt -CAcreateserial -days 365 -out client.crt
    )
  add_custom_target(generate_tls_configs DEPENDS ${SSL_CONFIGS})

  set(SSL_LIBRARY hiredis_ssl)
endif()

# Targets to setup Redis Clusters for testing
find_program(Docker_EXECUTABLE docker)
set(CONTAINER "bjosv/redis-cluster:latest") # When supporting password, change to: grokzen/redis-cluster:latest
add_custom_target(start
  COMMAND ${Docker_EXECUTABLE} run --name docker-cluster -d -p 7000-7006:7000-7006 ${CONTAINER} || (exit 0)
  COMMAND ${Docker_EXECUTABLE} run --name docker-cluster-passw -d -e INITIAL_PORT=7100 -e PASSWORD="secretword" -p 7100-7106:7100-7106 ${CONTAINER} || (exit 0)
)
add_custom_target(stop
  COMMAND ${Docker_EXECUTABLE} rm -f  docker-cluster || (exit 0)
  COMMAND ${Docker_EXECUTABLE} rm -f  docker-cluster-passw || (exit 0)
)

# Find dependencies
find_library(EVENT_LIBRARY event HINTS /usr/lib/x86_64-linux-gnu)

if(MSVC)
  # MS Visual: Suppress warnings
  add_compile_options("/wd 4267" "/wd 4244")
else()
  add_compile_options(-Wall -Wextra -pedantic -Werror)
endif()

# Debug mode for tests
set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "" FORCE)

add_executable(ct_async ct_async.c)
target_link_libraries(ct_async hiredis_cluster hiredis ${SSL_LIBRARY} ${EVENT_LIBRARY})
add_test(NAME ct_async COMMAND "$<TARGET_FILE:ct_async>")
set_tests_properties(ct_async PROPERTIES LABELS "CT")

add_executable(ct_commands ct_commands.c)
target_link_libraries(ct_commands hiredis_cluster hiredis ${SSL_LIBRARY})
add_test(NAME ct_commands COMMAND "$<TARGET_FILE:ct_commands>")
set_tests_properties(ct_commands PROPERTIES LABELS "CT")

add_executable(ct_connection ct_connection.c)
target_link_libraries(ct_connection hiredis_cluster hiredis ${SSL_LIBRARY} ${EVENT_LIBRARY})
add_test(NAME ct_connection COMMAND "$<TARGET_FILE:ct_connection>")
set_tests_properties(ct_connection PROPERTIES LABELS "CT")

add_executable(ct_pipeline ct_pipeline.c)
target_link_libraries(ct_pipeline hiredis_cluster hiredis ${SSL_LIBRARY} ${EVENT_LIBRARY})
add_test(NAME ct_pipeline COMMAND "$<TARGET_FILE:ct_pipeline>")
set_tests_properties(ct_pipeline PROPERTIES LABELS "CT")

# Executable: IPv6
add_executable(example_ipv6 main_ipv6.c)
target_link_libraries(example_ipv6 hiredis_cluster hiredis ${SSL_LIBRARY})

if(ENABLE_SSL)
  # Executable: tls
  add_executable(example_tls main_tls.c)
  target_link_libraries(example_tls hiredis_cluster hiredis ${SSL_LIBRARY})
  add_dependencies(example_tls generate_tls_configs)

  # Executable: async tls
  add_executable(example_async_tls main_async_tls.c)
  target_link_libraries(example_async_tls hiredis_cluster hiredis ${SSL_LIBRARY} ${EVENT_LIBRARY})
  add_dependencies(example_async_tls generate_tls_configs)
endif()
