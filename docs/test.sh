   # Test 1: Check if curl exists
   which curl
   echo "---"
   # Test 2: Try simple HTTP request
   curl -v "http://192.168.4.1/" 2>&1
   echo "---"
   # Test 3: Try file upload with simple filename (no spaces)
   echo "test" > /sdcard/test.txt
   curl -v -X POST -F "file=@/sdcard/test.txt" "http://192.168.4.1/upload?path=/" 2>&1
   