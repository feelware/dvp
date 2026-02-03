from fastapi import FastAPI
from fastapi.responses import JSONResponse
from tortoise import Tortoise, connections
from tortoise.exceptions import OperationalError
import boto3
import os
import logging
import asyncio
import asyncssh
import pika

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
      modules={"models": []},
    )
    await Tortoise.generate_schemas()
    logger.info("Connected to the database successfully.")
  except OperationalError as e:
    logger.error(f"Failed to connect to the database: {e}")

@app.on_event("shutdown")
async def shutdown_event():
  logger.info("Closing Tortoise ORM connection...")
  await connections.close_all()

@app.get("/test-db")
async def test_db_connection():
  try:
    connection = Tortoise.get_connection("default")
    await connection.execute_query("SELECT 1")
    return {
        "status": "success",
        "message": "Database connection is working."
    }
  except OperationalError as e:
    return JSONResponse(
      status_code=500,
      content={
        "status": "error",
        "message": str(e)
      }
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
    return {
      "status": "success",
      "message": "Storage connection is working."
    }
  except Exception as e:
    return JSONResponse(
      status_code=500,
      content={
        "status": "error",
        "message": str(e)
      }
    )

@app.get("/test-queue")
async def test_queue_connection():
  try:
    credentials = pika.PlainCredentials(RMQ_USER, RMQ_PASSWORD)
    connection = pika.BlockingConnection(
      pika.ConnectionParameters(
        host=RMQ_HOST,
        port=RMQ_PORT,
        credentials=credentials
      )
    )
    connection.close()
    return {
      "status": "success",
      "message": "Queue connection is working."
    }
  except Exception as e:
    return JSONResponse(
      status_code=500,
      content={
        "status": "error",
        "message": str(e)
      }
    )

@app.get("/test-mpi")
async def test_mpi_connection():
  try:
    async with asyncssh.connect(
      MPI_MASTER_HOST,
      username="mpiuser",
      client_keys=["/home/mpiuser/.ssh/id_rsa"]
    ) as conn:
      result = await conn.run("hostname", check=True)
      return {
        "status": "success",
        "message": f"MPI master node is reachable: {result.stdout.strip()}"
      }
  except Exception as e:
    return JSONResponse(
      status_code=500,
      content={
        "status": "error",
        "message": str(e)
      }
    )
