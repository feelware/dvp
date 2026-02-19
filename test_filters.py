import http.client
import sys
import json
import os
import uuid

API_HOST = "localhost"
API_PORT = 8000
API_PATH = "/upload"

def test_filter(video_path, task="grayscale"):
    if not os.path.exists(video_path):
        print(f"Error: File '{video_path}' not found.")
        return

    print(f"Uploading '{video_path}' for task: '{task}'...")
    
    boundary = uuid.uuid4().hex
    headers = {
        'Content-Type': f'multipart/form-data; boundary={boundary}'
    }
    
    body = []
    
    # Add Task part
    body.append(f'--{boundary}')
    body.append('Content-Disposition: form-data; name="task"')
    body.append('')
    body.append(task)
    
    # Add Params part
    body.append(f'--{boundary}')
    body.append('Content-Disposition: form-data; name="params"')
    body.append('')
    body.append('{}')
    
    # Add File part
    filename = os.path.basename(video_path)
    with open(video_path, 'rb') as f:
        file_content = f.read()
        
    body.append(f'--{boundary}')
    body.append(f'Content-Disposition: form-data; name="file"; filename="{filename}"')
    body.append('Content-Type: video/mp4')
    body.append('')
    # For binary content in mixed strings/bytes payload
    # We construct the payload as bytes
    
    # Re-construct body list as bytes
    payload_parts = []
    for part in body:
        if isinstance(part, str):
            payload_parts.append(part.encode('utf-8'))
        else:
            payload_parts.append(part)
    
    # Append file content (bytes)
    payload_parts.append(file_content)
    
    # Closing boundary
    payload_parts.append(f'\r\n--{boundary}--'.encode('utf-8'))
    payload_parts.append(b'')
    
    # Start with joined parts
    # Wait, the previous logic was simpler but potentially buggy with mixed types.
    # Let's do it clean:
    
    final_body = b''
    for part in body:
        if isinstance(part, str):
            final_body += part.encode('utf-8') + b'\r\n'
        else:
            # File content was not added to body list in loop above?
            # Correct logic:
            pass
            
    # Redo construction
    payload = b''
    for item in body:
        if isinstance(item, str):
            payload += item.encode('utf-8') + b'\r\n'
        elif isinstance(item, bytes):
            payload += item + b'\r\n'
            
    # That loop appended file content if it was in body.
    # In my previous attempt, I appended file content to body list?
    # No, I used `body.append(file_content.decode('latin1'))`. That was hacky.
    # Let's do it properly with bytes concatenation.
    
    payload = b''
    payload += f'--{boundary}\r\n'.encode()
    payload += f'Content-Disposition: form-data; name="task"\r\n\r\n'.encode()
    payload += f'{task}\r\n'.encode()
    
    payload += f'--{boundary}\r\n'.encode()
    payload += f'Content-Disposition: form-data; name="params"\r\n\r\n'.encode()
    payload += f'{{}}\r\n'.encode()
    
    payload += f'--{boundary}\r\n'.encode()
    payload += f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'.encode()
    payload += f'Content-Type: video/mp4\r\n\r\n'.encode()
    payload += file_content
    payload += b'\r\n'
    
    payload += f'--{boundary}--\r\n'.encode()

    try:
        conn = http.client.HTTPConnection(API_HOST, API_PORT)
        conn.request("POST", API_PATH, payload, headers)
        response = conn.getresponse()
        data = response.read().decode()
        
        if response.status == 200:
            result = json.loads(data)
            print("\n✅ Job Submitted Successfully!")
            job_id = result.get('job_id')
            print(f"Job ID: {job_id}")
            print(f"Response: {json.dumps(result, indent=2)}")
            
            print("\nNext Steps:")
            print(f"1. Check logs: docker logs -f mpi-master")
            print(f"2. Wait for confirmation in logs.")
            print(f"3. Retrieve output: docker cp mpi-master:/tmp/video_{job_id}.mp4 ./output_{task}.mp4")
        else:
            print(f"❌ Error: {response.status}")
            print(data)
            
        conn.close()
    except Exception as e:
        print(f"❌ Connection Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python test_filters.py <video.mp4> [task]")
        print("Tasks: grayscale, blur, invert, edge")
        sys.exit(1)
    
    video = sys.argv[1]
    task = sys.argv[2] if len(sys.argv) > 2 else "grayscale"
    test_filter(video, task)
