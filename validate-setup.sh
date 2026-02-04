#!/bin/bash

# Disable Git Bash path conversion on Windows
export MSYS_NO_PATHCONV=1

echo "=========================================="
echo "DVP System Validation Script"
echo "=========================================="
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Counters
PASSED=0
FAILED=0

# Function to check service
check_service() {
    local service=$1
    local description=$2

    echo -n "Checking $description... "
    if docker ps --format '{{.Names}}' | grep -q "^${service}$"; then
        echo -e "${GREEN}✓ RUNNING${NC}"
        ((PASSED++))
        return 0
    else
        echo -e "${RED}✗ NOT RUNNING${NC}"
        ((FAILED++))
        return 1
    fi
}

# Function to test endpoint
test_endpoint() {
    local endpoint=$1
    local description=$2

    echo -n "Testing $description... "
    response=$(curl -s -w "\n%{http_code}" http://localhost:8000${endpoint} 2>/dev/null)
    http_code=$(echo "$response" | tail -n1)
    body=$(echo "$response" | head -n-1)

    if [ "$http_code" = "200" ]; then
        status=$(echo "$body" | grep -o '"status":"[^"]*"' | cut -d'"' -f4)
        if [ "$status" = "success" ]; then
            echo -e "${GREEN}✓ PASSED${NC}"
            ((PASSED++))
            return 0
        else
            echo -e "${RED}✗ FAILED${NC}"
            echo "  Response: $body"
            ((FAILED++))
            return 1
        fi
    else
        echo -e "${RED}✗ FAILED (HTTP $http_code)${NC}"
        echo "  Response: $body"
        ((FAILED++))
        return 1
    fi
}

# Function to check SSH keys
check_ssh_keys() {
    local container=$1
    local description=$2

    echo -n "Checking SSH keys in $description... "

    # Check if id_rsa exists and is readable (as mpiuser)
    if docker exec -u mpiuser $container test -f /home/mpiuser/.ssh/id_rsa 2>/dev/null; then
        # Verify permissions
        perms=$(docker exec -u mpiuser $container stat -c "%a" /home/mpiuser/.ssh/id_rsa 2>/dev/null)
        if [ -n "$perms" ]; then
            echo -e "${GREEN}✓ FOUND (perms: $perms)${NC}"
            ((PASSED++))
            return 0
        else
            echo -e "${GREEN}✓ FOUND${NC}"
            ((PASSED++))
            return 0
        fi
    else
        echo -e "${RED}✗ NOT FOUND${NC}"
        # Debug: show what's in the directory (as mpiuser)
        echo "  Debug: ls /home/mpiuser/.ssh/"
        docker exec -u mpiuser $container ls -la /home/mpiuser/.ssh/ 2>&1 | sed 's/^/  /'
        ((FAILED++))
        return 1
    fi
}

# Function to test SSH connectivity
test_ssh_connection() {
    local from=$1
    local to=$2
    local description=$3

    echo -n "Testing $description... "
    # Test SSH connection as mpiuser
    result=$(docker exec -u mpiuser $from ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o BatchMode=yes mpiuser@$to hostname 2>&1)
    exit_code=$?

    if [ $exit_code -eq 0 ]; then
        echo -e "${GREEN}✓ CONNECTED${NC}"
        ((PASSED++))
        return 0
    else
        echo -e "${RED}✗ FAILED${NC}"
        echo "  Error: $result"
        ((FAILED++))
        return 1
    fi
}

echo "Step 1: Checking Docker Containers"
echo "------------------------------------------"
check_service "mpi-master" "MPI Master Node"
check_service "mpi-worker1" "MPI Worker 1"
check_service "mpi-worker2" "MPI Worker 2"
check_service "api" "API Server"
check_service "postgres" "PostgreSQL Database"
check_service "minio" "MinIO Storage"
check_service "rabbitmq" "RabbitMQ Queue"
echo ""

echo "Step 2: Checking SSH Keys"
echo "------------------------------------------"
check_ssh_keys "mpi-master" "MPI Master"
check_ssh_keys "mpi-worker1" "MPI Worker 1"
check_ssh_keys "mpi-worker2" "MPI Worker 2"
check_ssh_keys "api" "API Container"
echo ""

echo "Step 3: Testing SSH Connectivity"
echo "------------------------------------------"
test_ssh_connection "mpi-master" "master" "Master to Master (localhost)"
test_ssh_connection "mpi-master" "worker1" "Master to Worker 1"
test_ssh_connection "mpi-master" "worker2" "Master to Worker 2"
test_ssh_connection "mpi-worker1" "master" "Worker 1 to Master"
test_ssh_connection "mpi-worker2" "master" "Worker 2 to Master"
echo ""

echo "Step 4: Testing API Endpoints"
echo "------------------------------------------"
# Wait a bit for API to be fully ready
sleep 2
test_endpoint "/test-db" "Database Connection"
test_endpoint "/test-storage" "Storage Connection"
test_endpoint "/test-queue" "Queue Connection"
test_endpoint "/test-mpi" "MPI Connection"
echo ""

echo "Step 5: Testing MPI Execution"
echo "------------------------------------------"
echo -n "Testing MPI hostname command... "
# Create hostfile as mpiuser
docker exec -u mpiuser mpi-master bash -c "echo -e 'master slots=2\nworker1 slots=2\nworker2 slots=2' > /home/mpiuser/hostfile" 2>/dev/null
# Run MPI command as mpiuser
mpi_output=$(docker exec -u mpiuser mpi-master mpirun --hostfile /home/mpiuser/hostfile -np 6 hostname 2>&1)
exit_code=$?

if [ $exit_code -eq 0 ]; then
    # Count non-empty lines
    node_count=$(echo "$mpi_output" | grep -v '^$' | wc -l)
    if [ "$node_count" -ge 6 ]; then
        echo -e "${GREEN}✓ PASSED ($node_count processes)${NC}"
        ((PASSED++))
    else
        echo -e "${YELLOW}⚠ PARTIAL (only $node_count processes)${NC}"
        echo "  Output:"
        echo "$mpi_output" | sed 's/^/    /'
        ((FAILED++))
    fi
else
    echo -e "${RED}✗ FAILED${NC}"
    echo "  Error:"
    echo "$mpi_output" | sed 's/^/    /'
    ((FAILED++))
fi
echo ""

echo "=========================================="
echo "Validation Summary"
echo "=========================================="
echo -e "Passed: ${GREEN}$PASSED${NC}"
echo -e "Failed: ${RED}$FAILED${NC}"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}✓ All checks passed! System is ready.${NC}"
    echo ""
    echo "You can now:"
    echo "  - Access API docs: http://localhost:8000/docs"
    echo "  - Run MPI jobs from the API"
    echo "  - Access containers: docker exec -u mpiuser -it mpi-master bash"
    exit 0
else
    echo -e "${RED}✗ Some checks failed. Please review the errors above.${NC}"
    echo ""
    echo "Common troubleshooting steps:"
    echo "1. Check logs: docker-compose logs -f"
    echo "2. Rebuild API only: docker-compose build api && docker-compose up -d api"
    echo "3. Rebuild all: docker-compose down && docker-compose build --no-cache && docker-compose up -d"
    echo "4. Remove SSH volume: docker volume rm dvp_ssh-keys"
    echo "5. See README_DEPLOYMENT.md for detailed troubleshooting"
    exit 1
fi
