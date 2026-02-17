import asyncio
import json
import logging
import os
import uuid
from datetime import datetime
from io import BytesIO

import asyncssh
import boto3
import pika
from aio_pika import connect_robust, Message
from fastapi import FastAPI, File, Form, HTTPException, UploadFile
from fastapi.responses import JSONResponse
from tortoise import Tortoise, connections
from tortoise.exceptions import OperationalError

from .models import Job

S3_ACCESS_KEY_ID = os.environ["S3_ACCESS_KEY_ID"]
S3_SECRET_ACCESS_KEY = os.environ["S3_SECRET_ACCESS_KEY"]
S3_BUCKET_NAME = os.environ["S3_BUCKET_NAME"]
S3_ENDPOINT_URL = os.environ["S3_ENDPOINT_URL"]

POSTGRES_USER = os.environ["POSTGRES_USER"]
POSTGRES_PASSWORD = os.environ["POSTGRES_PASSWORD"]
POSTGRES_DB = os.environ["POSTGRES_DB"]
POSTGRES_HOST = os.environ["POSTGRES_HOST"]

RMQ_HOST = os.environ["RMQ_HOST"]
RMQ_PORT = int(os.environ["RMQ_PORT"])
RMQ_USER = os.environ["RMQ_USER"]
RMQ_PASSWORD = os.environ["RMQ_PASSWORD"]

MPI_MASTER_HOST = os.environ["MPI_MASTER_HOST"]

# Configuration
MAX_FILE_SIZE = 500 * 1024 * 1024  # 500 MB
ALLOWED_EXTENSIONS = {".mp4"}
RABBITMQ_QUEUE_NAME = "video_jobs"

# Set up logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s | (%(name)s) [%(levelname)s]: %(message)s",
    handlers=[logging.StreamHandler()],
)
logger = logging.getLogger(__name__)

app = FastAPI()


@app.on_event("startup")
async def startup_event():
    logger.info("Initializing Tortoise ORM...")
    try:
        await Tortoise.init(
            db_url=f"postgres://{POSTGRES_USER}:{POSTGRES_PASSWORD}@{POSTGRES_HOST}/{POSTGRES_DB}",
            modules={"models": ["src.models"]},
        )
        await Tortoise.generate_schemas()
        logger.info("Connected to the database successfully.")
    except OperationalError as e:
        logger.error(f"Failed to connect to the database: {e}")


@app.on_event("shutdown")
async def shutdown_event():
    logger.info("Closing Tortoise ORM connection...")
    await connections.close_all()


@app.get("/")
async def root():
    return {
        "service": "DVP - Distributed Video Processing API",
        "version": "1.0.0",
        "status": "running"
    }


@app.get("/test-db")
async def test_db_connection():
    try:
        connection = Tortoise.get_connection("default")
        await connection.execute_query("SELECT 1")
        return {"status": "success", "message": "Database connection is working."}
    except OperationalError as e:
        return JSONResponse(
            status_code=500, content={"status": "error", "message": str(e)}
        )


@app.get("/test-storage")
async def test_storage_connection():
    try:
        s3 = boto3.client(
            service_name="s3",
            aws_access_key_id=S3_ACCESS_KEY_ID,
            aws_secret_access_key=S3_SECRET_ACCESS_KEY,
            endpoint_url=S3_ENDPOINT_URL,
        )
        s3.list_buckets()
        return {"status": "success", "message": "Storage connection is working."}
    except Exception as e:
        return JSONResponse(
            status_code=500, content={"status": "error", "message": str(e)}
        )


@app.get("/test-queue")
async def test_queue_connection():
    try:
        credentials = pika.PlainCredentials(RMQ_USER, RMQ_PASSWORD)
        connection = pika.BlockingConnection(
            pika.ConnectionParameters(
                host=RMQ_HOST, port=RMQ_PORT, credentials=credentials
            )
        )
        connection.close()
        return {"status": "success", "message": "Queue connection is working."}
    except Exception as e:
        return JSONResponse(
            status_code=500, content={"status": "error", "message": str(e)}
        )


@app.get("/test-mpi")
async def test_mpi_connection():
    try:
        # Explicitly specify the known_hosts file path
        async with asyncssh.connect(
            MPI_MASTER_HOST,
            username="mpiuser",
            client_keys=["/home/mpiuser/.ssh/id_rsa"],
            known_hosts="/home/mpiuser/.ssh/known_hosts",
        ) as conn:
            # First test: simple hostname
            result = await conn.run("hostname", check=True)
            hostname_output = result.stdout.strip()

            # Second test: run mpirun across the cluster
            mpi_command = "mpirun -n 6 --host master:2,worker1:2,worker2:2 hostname"
            mpi_result = await conn.run(mpi_command, check=True)
            mpi_output = mpi_result.stdout.strip()

            return {
                "status": "success",
                "message": f"MPI master node is reachable: {hostname_output}",
                "mpi_test": {"command": mpi_command, "output": mpi_output},
            }
    except Exception as e:
        logger.error(f"MPI connection error: {str(e)}")
        return JSONResponse(
            status_code=500, content={"status": "error", "message": str(e)}
        )


def validate_video_file(filename: str, file_size: int) -> None:

    file_ext = os.path.splitext(filename)[1].lower()
    if file_ext not in ALLOWED_EXTENSIONS:
        raise HTTPException(
            status_code=400,
            detail=f"Invalid file format. Only MP4 files are allowed. Got: {file_ext}"
        )
    
    if file_size > MAX_FILE_SIZE:
        raise HTTPException(
            status_code=400,
            detail=f"File too large. Maximum size is {MAX_FILE_SIZE // (1024*1024)} MB"
        )


async def upload_to_minio(file_content: bytes, object_name: str) -> str:
    """
    Upload file to MinIO storage
    Returns the path in MinIO

    Stevan confirma si esto está bien w
    """
    try:
        s3 = boto3.client(
            service_name="s3",
            aws_access_key_id=S3_ACCESS_KEY_ID,
            aws_secret_access_key=S3_SECRET_ACCESS_KEY,
            endpoint_url=S3_ENDPOINT_URL,
        )
        
        file_obj = BytesIO(file_content)
        s3.upload_fileobj(
            file_obj,
            S3_BUCKET_NAME,
            object_name,
            ExtraArgs={'ContentType': 'video/mp4'}
        )
        
        video_path = f"{S3_BUCKET_NAME}/{object_name}"
        logger.info(f"File uploaded to MinIO: {video_path}")
        return video_path
        
    except Exception as e:
        logger.error(f"Failed to upload to MinIO: {str(e)}")
        raise HTTPException(
            status_code=500,
            detail=f"Failed to upload video to storage: {str(e)}"
        )


async def send_to_rabbitmq(message_data: dict) -> None:
    """
    Send message to RabbitMQ queue
    """
    try:
        connection = await connect_robust(
            host=RMQ_HOST,
            port=RMQ_PORT,
            login=RMQ_USER,
            password=RMQ_PASSWORD,
        )
        
        async with connection:
            channel = await connection.channel()
            
            queue = await channel.declare_queue(
                RABBITMQ_QUEUE_NAME,
                durable=True  # Queue survives broker restart
            )
            
            message_body = json.dumps(message_data).encode()
            message = Message(
                message_body,
                delivery_mode=2,  
            )
            
            await channel.default_exchange.publish(
                message,
                routing_key=RABBITMQ_QUEUE_NAME,
            )
            
            logger.info(f"Message sent to RabbitMQ queue '{RABBITMQ_QUEUE_NAME}': {message_data['job_id']}")
            
    except Exception as e:
        logger.error(f"Failed to send message to RabbitMQ: {str(e)}")
        raise HTTPException(
            status_code=500,
            detail=f"Failed to queue processing task: {str(e)}"
        )


@app.post("/upload")
async def upload_video(
    file: UploadFile = File(..., description="MP4 video file to process"),
    task: str = Form(..., description="Processing task (e.g., 'convert', 'compress', 'extract_frames')"),
    params: str = Form(..., description="JSON string with task parameters")
):
    """
    Upload a video file for distributed processing
    
    **Parameters:**
    - **file**: MP4 video file (max 500MB)
    - **task**: Type of processing task
    - **params**: JSON parameters for the task
    
    **Example params:**
    ```json
    {
        "output_format": "webm",
        "resolution": "720p",
        "bitrate": "2M"
    }
    ```
    
    **Returns:**
    - job_id: Unique identifier for tracking the job
    - video_path: Location of the uploaded video in storage
    - status: Current job status
    """
    try:
        try:
            params_dict = json.loads(params)
        except json.JSONDecodeError:
            raise HTTPException(
                status_code=400,
                detail="Invalid JSON in params field"
            )
        
        file_content = await file.read()
        file_size = len(file_content)
        
        validate_video_file(file.filename, file_size)
        
        job_id = str(uuid.uuid4())
        
        timestamp = datetime.utcnow().strftime("%Y%m%d")
        file_extension = os.path.splitext(file.filename)[1]
        object_name = f"uploads/{timestamp}/video_{job_id}{file_extension}"
        

        video_path = await upload_to_minio(file_content, object_name)
        
        job = await Job.create(
            job_id=job_id,
            video_path=video_path,
            task=task,
            params=params_dict,
            status="pending"
        )
        
        logger.info(f"Created job record in database: {job_id}")
        
        message_data = {
            "job_id": job_id,
            "video_path": video_path,
            "task": task,
            "params": params_dict,
            "created_at": job.created_at.isoformat()
        }
        
        await send_to_rabbitmq(message_data)
        
        return {
            "status": "success",
            "job_id": job_id,
            "video_path": video_path,
            "task": task,
            "params": params_dict,
            "message": "Video uploaded successfully and queued for processing",
            "created_at": job.created_at.isoformat()
        }
        
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"Unexpected error in upload endpoint: {str(e)}")
        raise HTTPException(
            status_code=500,
            detail=f"Internal server error: {str(e)}"
        )


@app.get("/jobs/{job_id}")
async def get_job_status(job_id: str):
    """
    Get the status of a processing job
    """
    try:
        job = await Job.get_or_none(job_id=job_id)
        
        if not job:
            raise HTTPException(
                status_code=404,
                detail=f"Job not found: {job_id}"
            )
        
        return {
            "job_id": job.job_id,
            "video_path": job.video_path,
            "task": job.task,
            "params": job.params,
            "status": job.status,
            "created_at": job.created_at.isoformat(),
            "updated_at": job.updated_at.isoformat(),
            "error_message": job.error_message
        }
        
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"Error getting job status: {str(e)}")
        raise HTTPException(
            status_code=500,
            detail=f"Failed to retrieve job status: {str(e)}"
        )


@app.get("/jobs")
async def list_jobs(
    status: str = None,
    limit: int = 50,
    offset: int = 0
):
    """
    List all jobs, optionally filtered by status
    
    **Parameters:**
    - status: Filter by job status (pending, processing, completed, failed)
    - limit: Maximum number of results (default: 50)
    - offset: Number of results to skip (default: 0)
    """
    try:
        query = Job.all()
        
        if status:
            query = query.filter(status=status)
        
        total = await query.count()
        jobs = await query.offset(offset).limit(limit).order_by('-created_at')
        
        return {
            "total": total,
            "limit": limit,
            "offset": offset,
            "jobs": [
                {
                    "job_id": job.job_id,
                    "task": job.task,
                    "status": job.status,
                    "created_at": job.created_at.isoformat(),
                    "updated_at": job.updated_at.isoformat()
                }
                for job in jobs
            ]
        }
        
    except Exception as e:
        logger.error(f"Error listing jobs: {str(e)}")
        raise HTTPException(
            status_code=500,
            detail=f"Failed to list jobs: {str(e)}"
        )