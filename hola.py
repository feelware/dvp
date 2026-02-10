#!/usr/bin/env python3
"""
Test script for the /upload endpoint
"""
import json
import requests

# API URL
API_URL = "http://localhost:8000"

def test_upload_endpoint():
    """
    Test the video upload endpoint
    """
    # Create test parameters
    params = {
        "output_format": "webm",
        "resolution": "720p",
        "bitrate": "2M"
    }
    
    # Prepare the request
    # NOTE: Replace 'test_video.mp4' with an actual MP4 file
    with open('test_video.mp4', 'rb') as video_file:
        files = {
            'file': ('test_video.mp4', video_file, 'video/mp4')
        }
        
        data = {
            'task': 'convert',
            'params': json.dumps(params)
        }
        
        print("Uploading video...")
        response = requests.post(f"{API_URL}/upload", files=files, data=data)
        
        if response.status_code == 200:
            result = response.json()
            print("✓ Upload successful!")
            print(f"Job ID: {result['job_id']}")
            print(f"Video Path: {result['video_path']}")
            print(f"Status: {result['status']}")
            return result['job_id']
        else:
            print(f"✗ Upload failed: {response.status_code}")
            print(response.text)
            return None


def test_get_job_status(job_id):
    """
    Test getting job status
    """
    print(f"\nGetting status for job: {job_id}")
    response = requests.get(f"{API_URL}/jobs/{job_id}")
    
    if response.status_code == 200:
        result = response.json()
        print("✓ Job status retrieved!")
        print(json.dumps(result, indent=2))
    else:
        print(f"✗ Failed to get job status: {response.status_code}")
        print(response.text)


def test_list_jobs():
    """
    Test listing all jobs
    """
    print("\nListing all jobs...")
    response = requests.get(f"{API_URL}/jobs?limit=10")
    
    if response.status_code == 200:
        result = response.json()
        print(f"✓ Found {result['total']} jobs")
        print(json.dumps(result, indent=2))
    else:
        print(f"✗ Failed to list jobs: {response.status_code}")
        print(response.text)


if __name__ == "__main__":
    print("=" * 50)
    print("DVP API Upload Endpoint Test")
    print("=" * 50)
    
    # Test upload
    job_id = test_upload_endpoint()
    
    if job_id:
        # Test get job status
        test_get_job_status(job_id)
        
        # Test list jobs
        test_list_jobs()
    
    print("\n" + "=" * 50)
    print("Test completed!")
    print("=" * 50)