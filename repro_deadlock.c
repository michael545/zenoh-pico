#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <process.h>

//Fake threadx
#define TX_SUCCESS 0
#define TX_WAIT_FOREVER 0xFFFFFFFF
#define TX_NO_WAIT 0

typedef void* TX_SEMAPHORE; 
volatile LONG g_lock = 0;    
DWORD g_isr_thread_id = 0;

// Custom Spinlock to simulate RTOS Mutex
int acquire_lock(unsigned long wait_option) {
    if (GetCurrentThreadId() == g_isr_thread_id && wait_option == TX_WAIT_FOREVER) {
        while (InterlockedCompareExchange(&g_lock, 1, 0) != 0) {
            // On real hardware, this is where the CPU hangs.
        }
        return TX_SUCCESS;
    } else {
        // APP CONTEXT: Normal Wait
        // We can be nice here and yield if we can't get it.
        while (InterlockedCompareExchange(&g_lock, 1, 0) != 0) {
            Sleep(1); // Yield to let ISR run (if it wants to)
        }
        return TX_SUCCESS;
    }
}

void release_lock() {
    InterlockedExchange(&g_lock, 0);
}

// Mocking the TX API with our custom lock
void tx_semaphore_create(TX_SEMAPHORE* sem, char* name, int initial_count) { } // No-op

int tx_semaphore_get(TX_SEMAPHORE* sem, unsigned long wait_option) {
    // We only care about the data_processing_semaphore for the deadlock
    // We map the semaphore logic to our raw lock for demonstration
    return acquire_lock(wait_option);
}

void tx_semaphore_put(TX_SEMAPHORE* sem) {
    release_lock();
}

const int PROCESSING_TIME_MS = 20; //strcopy can take some time on the mcu 
const int PACKET_INTERVAL_MS = 5;   

TX_SEMAPHORE data_ready_semaphore;      
TX_SEMAPHORE data_processing_semaphore; 

// ==========================================
// 3. THE APPLICATION THREAD (Consumer)
// ==========================================
unsigned __stdcall application_thread(void* arg) {
    // Set to LOWEST priority to ensure ISR can starve it
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST)) {
        printf("WARNING: Failed to set App priority!\n");
    }
    SetThreadPriorityBoost(GetCurrentThread(), TRUE);
    
    printf("[APP] Thread started (Low Priority). Waiting for data...\n");
    
    while (1) {
        // Simple synchronization for the test
        while (g_lock == 1) Sleep(1); // Wait for ISR to release (if it held it)

        // Lock the buffer
        acquire_lock(TX_WAIT_FOREVER);
        
        printf("[APP] Processing packet... (Busy for %d ms)\n", PROCESSING_TIME_MS);
        
        // CRITICAL SECTION
        // Use BUSY WAIT instead of Sleep to ensure we hold the CPU until preempted
        DWORD start = GetTickCount();
        while (GetTickCount() - start < PROCESSING_TIME_MS) {
            // Burn CPU cycles
            volatile int x = 0;
            x++;
        }
        
        release_lock();
        printf("[APP] Packet processed. Buffer unlocked.\n");
        
        // Wait for next packet signal (simulated)
        Sleep(1);
    }
    return 0;
}

// ==========================================
// 4. THE ISR (Producer / Interrupt)
// ==========================================
void simulated_isr(int packet_id) {
    printf("\n[ISR] HARDWARE INTERRUPT! Packet #%d arrived.\n", packet_id);
    printf("[ISR] Attempting to acquire buffer lock (Spinning)...\n");
    
    // This will now SPIN if the lock is held, starving the App thread.
    if (acquire_lock(TX_WAIT_FOREVER) == TX_SUCCESS) {
        printf("[ISR] Lock acquired. Updating buffer pointers.\n");
        release_lock();
    }
}


int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    // 1. FORCE SINGLE CORE EXECUTION (Crucial for deadlock simulation)
    if (!SetProcessAffinityMask(GetCurrentProcess(), 1)) {
        printf("WARNING: Failed to set Affinity!\n");
    }
    
    // 2. MAKE MAIN THREAD "TIME CRITICAL" (Simulates ISR Priority)
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)) {
        printf("WARNING: Failed to set ISR priority!\n");
    }
    g_isr_thread_id = GetCurrentThreadId();

    // tx_semaphore_create calls are no-ops now
    
    printf("--- STARTING REALISTIC DEADLOCK SIMULATION ---\n");
    printf("Config: Single Core Affinity, Priority Inversion Enforced.\n");
    
    HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, application_thread, NULL, 0, NULL);
    
    // Give App time to start waiting
    Sleep(100);

    for (int i = 1; i <= 5; i++) {
        simulated_isr(i);
        Sleep(PACKET_INTERVAL_MS);
    }
    
    printf("\n--- SIMULATION SURVIVED (Unexpected) ---\n");
    
    CloseHandle(hThread);
    return 0;
}
