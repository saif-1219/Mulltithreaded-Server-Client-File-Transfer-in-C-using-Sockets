#!/bin/bash

# Path to server and client executables
SERVER_EXEC="./server"
CLIENT_EXEC="./client"

# Start the server in the background
start_server() {
    echo "Starting server..."
    $SERVER_EXEC &
    SERVER_PID=$!
    echo "Server started with PID $SERVER_PID"
    sleep 2  # Give server time to start
}

# Stop the server
stop_server() {
    echo "Stopping server..."
    kill $SERVER_PID
    echo "Server stopped."
}

# Run a single test case
run_test() {
    local file_name=$1
    local threads=$2

    echo "Running test with file: $file_name, threads: $threads" 

    # Calculate original file SHA256 checksum
    original_checksum=$(sha256sum "./$file_name" | awk '{print $1}')
    echo "Original SHA256 checksum: $original_checksum" 

    # Run the client to request the file transfer
    $CLIENT_EXEC "$file_name" "$threads"

    sleep 2  # Give time for the file to be reassembled

    # Check if the result file exists
    result_file="${file_name%.*}_at_client.${file_name##*.}"
    if [[ ! -f "./$result_file" ]]; then
        echo "Test failed: Result file not found" 
        return 1
    fi

    # Calculate SHA256 checksum of the reassembled file
    reassembled_checksum=$(sha256sum "./$result_file" | awk '{print $1}')
    echo "Reassembled SHA256 checksum: $reassembled_checksum" 

    # Compare checksums
    if [[ "$original_checksum" == "$reassembled_checksum" ]]; then
        echo "Test passed!" 
    else
        echo "Test failed: Checksums do not match" 
        return 1
    fi
}

# Main function to execute all tests
main() {
    start_server

    # Define test cases
    declare -a tests=(
        "simple_text_1kb.txt 1"
        "simple_text_1kb.txt 5"
        "large_text_10mb.txt 10"
        "small_image_50kb.jpg 5"
        "large_image_500kb.jpg 10"
        "small_video_1mb.m4v 10"
        "moderate_video_10mb.m4v 20"
        "compressed_file_19mb.zip 10"
        "corrupted_file_2mb.txt 8"
    )

    # Run all tests
    for test in "${tests[@]}"; do
        set -- $test
        run_test "$1" "$2"
        sleep 1
    done

    stop_server
}

# Execute the main function
main
