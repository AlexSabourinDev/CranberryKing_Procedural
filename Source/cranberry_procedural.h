#ifndef CRANBERRY_PROCEDURAL
#define CRANBERRY_PROCEDURAL

typedef enum
{
	cranp_op_id_circle = 0,
	cranp_op_id_translate,
	cranp_op_id_max
} cranp_op_id_e;

typedef struct _cranp_vm_t cranp_vm_t;

typedef void(*cranp_opf_t)(cranp_vm_t* vm, unsigned int slotId,  unsigned int* inputBuffers, unsigned int inputBufferCount, void* params);

// cranp_script_t is a large memory buffer that is used by the virtual machine (or interpreter if you want to call it that)
// to actually run scripts. It's format looks similarly to [collectionSize, collection, collectionSize, collection]
// This means that we can't advance a constant amount, but our memory is fairly elegantly laid out.

// Currently, memory for ops in cranberry_procedural is simple. Every op gets the same amount of memory. This means that some ops will not
// make good use of their memory slot, but also allows allocations, deallocations and memory accesses to be fast. As a result, at the cost of
// more memory, the usability and performance are pretty good.

// NOTE: Meshes are always output with counter clockwise winding

//
// cranp_script_t memory format:
// op count
// op funcs
// op slot Ids
// input size
// inputs (input count + input slots)
// params (param size(includes sizeof(param size)) + params)
//
// TODO: Adding an "expose" feature to the scripts would be cool. We could simply have a pair of "Type, param offset"
// to allow changing of a loaded scripts parameters
typedef struct _cranp_script_t cranp_script_t;

void cranp_init(void);

// TODO: ops do a lot of memory management that takes away from their actual logic. Can we clean that up somehow?
unsigned long long cranp_vm_buffer_size(unsigned long long memorySize, unsigned int maxActiveBuffers);
cranp_vm_t* cranp_vm_buffer_create(void* buffer, unsigned long long memorySize, unsigned int maxActiveBuffers);
void* cranp_vm_alloc_chunk(cranp_vm_t* vm, unsigned int slotId, unsigned long long memorySize);
void* cranp_vm_get_chunk(cranp_vm_t* vm, unsigned int slotId);

void cranp_init_script(cranp_script_t* script);
void cranp_vm_execute_script(cranp_vm_t* vm, cranp_script_t* script);

#endif // CRANBERRY_PROCEDURAL

#ifdef CRANBERRY_PROCEDURAL_TEST
void cranp_test(void);
#endif // CRANBERRY_PROCEDURAL_TESTS

#ifdef CRANBERRY_PROCEDURAL_IMPLEMENTATION

#include "cranberry_math.h"

#include <stdint.h>
#include <string.h>

#ifdef CRANBERRY_DEBUG
#include <assert.h>
#include <stdio.h>

#define cranp_assert(a) assert(a)
#define cranp_log(a, ...) printf(a, __VA_ARGS__)
#else
#define cranp_assert(a)
#define cranp_log(a, ...)
#endif // CRANBERRY_DEBUG

#define cranp_potentially_unused(a) (void)a
#define cranp_alignment 16

typedef struct
{
	uint64_t allocChunkCount;
	uint64_t allocChunkSize;
	void* buffer;
} cranp_allocator_t;

typedef struct
{
	uint16_t idx[3];
} cranp_triangle_t;

/*
Buffer Layout:

- Memory -
cranp_allocator
*/

void* cranp_advance(void* ptr, uint64_t offset)
{
	return ((uint8_t*)ptr + offset);
}

uint64_t cranp_allocator_buffer_size(uint64_t memorySize, uint32_t chunkCount)
{
	uint64_t chunkSize = memorySize / chunkCount;
	return sizeof(uint64_t) * 2 + chunkSize * chunkCount + cranp_alignment * chunkCount;
}

void cranp_allocator_buffer_create(void* buffer, uint64_t memorySize, uint32_t maxActiveBuffers)
{
	*(uint64_t*)buffer = maxActiveBuffers;
	*(uint64_t*)cranp_advance(buffer, sizeof(uint64_t)) = memorySize / maxActiveBuffers;
}

cranp_allocator_t cranp_view_as_allocator(void* buffer)
{
	return (cranp_allocator_t)
	{
		.allocChunkCount = *(uint64_t*)buffer,
		.allocChunkSize = *(uint64_t*)cranp_advance(buffer, sizeof(uint64_t)),
		.buffer = cranp_advance(buffer, sizeof(uint64_t) * 2)
	};
}

void* cranp_allocator_get_chunk(cranp_allocator_t* allocator, unsigned int slotId)
{
	// We don't have that many chunks!
	cranp_assert(slotId < allocator->allocChunkCount);
	intptr_t chunkAddress = (intptr_t)cranp_advance(allocator->buffer, slotId * allocator->allocChunkSize);
	chunkAddress += cranp_alignment - chunkAddress % cranp_alignment;
	return (void*)chunkAddress;
}

void cranp_register_ops(void);
void cranp_init(void)
{
	cranp_register_ops();
}

// Memory
cranp_allocator_t cranp_vm_get_allocator(cranp_vm_t* vm)
{
	return cranp_view_as_allocator(vm);
}

unsigned long long cranp_vm_buffer_size(unsigned long long memorySize, unsigned int maxActiveBuffers)
{
	cranp_assert(memorySize % cranp_alignment == 0); // Chunk size must be a multiple of our alignment!
	return cranp_allocator_buffer_size(memorySize, maxActiveBuffers);
}

cranp_vm_t* cranp_vm_buffer_create(void* buffer, unsigned long long memorySize, unsigned int maxActiveBuffers)
{
	cranp_allocator_buffer_create(buffer, memorySize, maxActiveBuffers);
	return (cranp_vm_t*)buffer;
}

void* cranp_vm_alloc_chunk(cranp_vm_t* vm, unsigned int slotId, unsigned long long chunkSize)
{
	cranp_potentially_unused(chunkSize);

	cranp_allocator_t allocator = cranp_vm_get_allocator(vm);
	cranp_assert(chunkSize <= allocator.allocChunkSize);

	return cranp_allocator_get_chunk(&allocator, slotId);
}

void* cranp_vm_get_chunk(cranp_vm_t* vm, unsigned int slotId)
{
	cranp_allocator_t allocator = cranp_vm_get_allocator(vm);
	return cranp_allocator_get_chunk(&allocator, slotId);
}

// Interpreter
void cranp_vm_execute_script(cranp_vm_t* vm, cranp_script_t* script)
{
	uint32_t opCount = *(uint32_t*)script;

	cranp_opf_t* ops = (cranp_opf_t*)cranp_advance(script, sizeof(uint32_t));
	cranp_opf_t* opEnd = ops + opCount;

	uint32_t* slotIds = (uint32_t*)cranp_advance(script, sizeof(uint32_t) + opCount * sizeof(cranp_opf_t));

	uint32_t* inputs = (uint32_t*)cranp_advance(script, sizeof(uint32_t) + opCount * sizeof(cranp_opf_t) + opCount * sizeof(uint32_t));
	uint32_t inputSize = *inputs;
	inputs++;

	uint32_t* params = (uint32_t*)cranp_advance(script, sizeof(uint32_t) + opCount * sizeof(cranp_opf_t) + opCount * sizeof(uint32_t) + inputSize);

	// Process first few ops
	for (uint32_t i = 0; i < opCount % 4; ++i)
	{
		uint32_t inputSlotCount = *inputs;
		ops[0](vm, slotIds[0], inputs + 1, inputSlotCount, params + 1);

		ops++;
		slotIds++;

		uint32_t paramSize = *params;
		params = (uint32_t*)cranp_advance(params, paramSize);
		inputs = (uint32_t*)cranp_advance(inputs, (inputSlotCount + 1) * sizeof(uint32_t));
	}

	while (ops != opEnd)
	{
		uint32_t inputSlotCount = *inputs;
		uint32_t paramSize = *params;

		ops[0](vm, slotIds[0], inputs + 1, inputSlotCount, params + 1);
		inputs = (uint32_t*)cranp_advance(inputs, sizeof(uint32_t) * (inputSlotCount + 1));
		inputSlotCount = *inputs;
		params = cranp_advance(params, paramSize);
		paramSize = *params;

		ops[1](vm, slotIds[1], inputs + 1, inputSlotCount, params + 1);
		inputs = (uint32_t*)cranp_advance(inputs, sizeof(uint32_t) * (inputSlotCount + 1));
		inputSlotCount = *inputs;
		params = cranp_advance(params, paramSize);
		paramSize = *params;

		ops[2](vm, slotIds[2], inputs + 1, inputSlotCount, params + 1);
		inputs = (uint32_t*)cranp_advance(inputs, sizeof(uint32_t) * (inputSlotCount + 1));
		inputSlotCount = *inputs;
		params = cranp_advance(params, paramSize);
		paramSize = *params;

		ops[3](vm, slotIds[3], inputs + 1, inputSlotCount, params + 1);
		inputs = (uint32_t*)cranp_advance(inputs, sizeof(uint32_t) * (inputSlotCount + 1));
		inputSlotCount = *inputs;
		params = cranp_advance(params, paramSize);

		slotIds += 4;
		ops += 4;
	}
}

// Ops
static cranp_opf_t cranp_op_table[cranp_op_id_max];

void cranp_init_script(cranp_script_t* script)
{
	uint32_t opCount = *(uint32_t*)script;

	cranp_opf_t* ops = (cranp_opf_t*)cranp_advance(script, sizeof(uint32_t));
	for (uint32_t i = 0; i < opCount; ++i)
	{
		intptr_t opId = (intptr_t)ops[i];
		cranp_assert(opId < cranp_op_id_max);
		ops[i] = cranp_op_table[opId];
	}
}

void cranp_op_circle(cranp_vm_t* vm, unsigned int slotId, unsigned int* inputs, unsigned int inputCount, void* params)
{
	// Circle doesn't take any inputs
	cranp_assert(inputCount == 0);

	cranp_potentially_unused(inputs);
	cranp_potentially_unused(inputCount);

	float segmentCount = *(float*)params;
	float radius = *((float*)params + 1);

	cranp_assert(segmentCount >= 3.0f);

	uint32_t allocSize =
		sizeof(cranm_vec_t) // vertex count, triangle count, unused, unused
		+ ((uint32_t)segmentCount + 1) * sizeof(cranm_vec_t) // vertices
		+ (uint32_t)segmentCount * sizeof(cranp_triangle_t); // triangles

	void* writeHead = cranp_vm_alloc_chunk(vm, slotId, allocSize);
	
	// Write vert count and triangle count
	*(cranm_vec_t*)writeHead = (cranm_vec_t){.x = segmentCount + 1.0f, .y = segmentCount };
	writeHead = cranp_advance(writeHead, sizeof(cranm_vec_t));

	{
		*(cranm_vec_t*)writeHead = (cranm_vec_t) { .x = 0.0f, .y = 0.0f, .z = 0.0f };
		writeHead = cranp_advance(writeHead, sizeof(cranm_vec_t));

		// Mesh first
		float angleIncrement = 2.0f * cranm_pi / segmentCount;
		for (float segment = 0.0f; segment < segmentCount; segment += 1.0f)
		{
			*(cranm_vec_t*)writeHead = (cranm_vec_t) { .x = cosf(angleIncrement * segment) * radius, .y = sinf(angleIncrement * segment) * radius, .z = 0.0f };
			writeHead = cranp_advance(writeHead, sizeof(cranm_vec_t));
		}
	}

	{
		// Triangles next
		uint32_t triangleCount = (uint32_t)segmentCount;
		for (uint16_t triangle = 0; triangle < triangleCount; triangle++)
		{
			*(cranp_triangle_t*)writeHead = (cranp_triangle_t){0, triangle + 1, triangle + 2};
			writeHead = cranp_advance(writeHead, sizeof(cranp_triangle_t));
		}
	}
}

void cranp_op_translate(cranp_vm_t* vm, unsigned int slotId, unsigned int* inputs, unsigned int inputCount, void* params)
{
	cranp_assert(inputCount == 1);

	cranm_vec_t translation = *(cranm_vec_t*)params;

	void* readHead = cranp_vm_get_chunk(vm, inputs[0]);
	cranm_vec_t counts = *(cranm_vec_t*)readHead;
	readHead = cranp_advance(readHead, sizeof(cranm_vec_t));

	uint32_t vertCount = (uint32_t)counts.x;
	uint32_t triangleCount = (uint32_t)counts.y;
	uint32_t allocSize = sizeof(cranm_vec_t) + vertCount * sizeof(cranm_vec_t) + triangleCount * sizeof(cranp_triangle_t);
	
	void* writeHead = cranp_vm_alloc_chunk(vm, slotId, allocSize);
	// Write vert count and triangle count
	*(cranm_vec_t*)writeHead = counts;
	writeHead = cranp_advance(writeHead, sizeof(cranm_vec_t));

	{
		// Mesh first
		for (uint32_t vert = 0; vert < vertCount; vert++)
		{
			*(cranm_vec_t*)writeHead = cranm_add3(*(cranm_vec_t*)readHead, translation);
			writeHead = cranp_advance(writeHead, sizeof(cranm_vec_t));
			readHead = cranp_advance(readHead, sizeof(cranm_vec_t));
		}
	}

	memcpy(writeHead, readHead, (uint32_t)triangleCount * sizeof(cranp_triangle_t));
}

void cranp_register_ops(void)
{
	cranp_op_table[cranp_op_id_circle] = cranp_op_circle;
	cranp_op_table[cranp_op_id_translate] = cranp_op_translate;
}

#ifdef CRANBERRY_PROCEDURAL_TESTS

#include "Mist_Profiler.h"

const char* cranp_test_script_ops[] = 
{
	[cranp_op_id_circle] = "circle",
	[cranp_op_id_translate] = "translate"
};

const char* cranp_test_script_basic =
"circle 0 [] [10000.0,10.0]\n"
"translate 1 [0] [10.0,10.0,10.0,0.0f]\n";

cranp_script_t* cranp_test_parse_script(const char* script)
{
	typedef enum
	{
		cranp_parse_op,
		cranp_parse_id,
		cranp_parse_inputs,
		cranp_parse_params
	} cranp_test_parser_state_e;

	intptr_t* opChunk = (intptr_t*)malloc(sizeof(cranp_opf_t) * 100);
	uint32_t opCount = 0;

	uint32_t* opIdChunk = (uint32_t*)malloc(sizeof(uint32_t) * 100);
	uint32_t opIdCount = 0;

	uint32_t* inputChunk = (uint32_t*)malloc(sizeof(uint32_t) * (100 * 2 + 1));
	uint32_t inputWriteCount = 0;

	uint8_t* paramChunk = (uint8_t*)malloc(sizeof(uint32_t) * 100 * 2);
	uint32_t paramWriteSize = 0;

	cranp_test_parser_state_e parserState = cranp_parse_op;

	char const* prevIter = script;
	for (const char* iter = script; *iter != '\0'; iter++)
	{
		if (*iter == ' ' || *iter == '\n')
		{
			switch (parserState)
			{
			case cranp_parse_op:
			{
				cranp_op_id_e op = cranp_op_id_max;
				for (uint32_t i = 0; i < cranp_op_id_max; i++)
				{
					const char* opIter = cranp_test_script_ops[i];
					const char* parsedOpIter = prevIter;
					for (; parsedOpIter != iter && *opIter != '\0'; parsedOpIter++, opIter++)
					{
						if (*opIter != *parsedOpIter)
						{
							break;
						}
					}

					// We found our op
					if (*opIter == '\0' && parsedOpIter == iter)
					{
						op = i;
						break;
					}
				}
				cranp_assert(op != cranp_op_id_max);

				opChunk[opCount++] = (intptr_t)op;

				parserState = cranp_parse_id;
				prevIter = iter + 1; // We want to skip the ' '
			}
			break;

			case cranp_parse_id:
			{
				uint32_t id = (uint32_t)atoi(prevIter);
				opIdChunk[opIdCount++] = id;

				parserState = cranp_parse_inputs;
				prevIter = iter + 1; // We want to skip the ' '
			}
			break;

			case cranp_parse_inputs:
			{
				assert(*prevIter == '[');

				uint32_t inputCount = 0;

				const char* lastNumberStart = prevIter + 1;
				const char* numberIter = lastNumberStart;
				while (1)
				{
					if (*numberIter == ',' || (*numberIter == ']' && *lastNumberStart != ']'))
					{
						inputChunk[inputWriteCount + inputCount + 1] = (uint32_t)atoi(lastNumberStart);
						inputCount++;

						lastNumberStart = numberIter + 1;
					}

					if (*numberIter == ']')
					{
						break;
					}

					numberIter++;
				}

				inputChunk[inputWriteCount] = inputCount;
				inputWriteCount += inputCount + 1;

				parserState = cranp_parse_params;
				prevIter = iter + 1; // We want to skip " "
			}
			break;

			case cranp_parse_params:
			{
				assert(*prevIter == '[');

				uint32_t paramSize = 0;

				const char* lastNumberStart = prevIter + 1;
				const char* numberIter = lastNumberStart;
				while (1)
				{
					if (*numberIter == ',' || *numberIter == ']')
					{
						*(float*)(paramChunk + (paramWriteSize + paramSize + sizeof(uint32_t))) = (float)atof(lastNumberStart);
						paramSize += sizeof(float);

						lastNumberStart = numberIter + 1;
					}

					if (*numberIter == ']')
					{
						break;
					}

					numberIter++;
				}

				*(uint32_t*)(paramChunk + paramWriteSize) = paramSize + sizeof(uint32_t);
				paramWriteSize += paramSize + sizeof(uint32_t);

				parserState = cranp_parse_op;
				prevIter = iter + 1; // We want to skip " "
			}
			break;
			}
		}
	}

	void* scriptBuffer = 
		malloc(sizeof(uint32_t) 
			+ sizeof(cranp_opf_t) * opCount 
			+ sizeof(uint32_t) * opCount 
			+ sizeof(uint32_t)
			+ sizeof(uint32_t) * inputWriteCount 
			+ paramWriteSize);

	uint8_t* compiledScript = scriptBuffer;
	*(uint32_t*)compiledScript = opCount;
	compiledScript += sizeof(uint32_t);
	memcpy(compiledScript, opChunk, sizeof(cranp_opf_t) * opCount);
	compiledScript += sizeof(cranp_opf_t) * opCount;
	memcpy(compiledScript, opIdChunk, sizeof(uint32_t) * opCount);
	compiledScript += sizeof(uint32_t) * opCount;

	*(uint32_t*)compiledScript = inputWriteCount * sizeof(uint32_t) + sizeof(uint32_t);
	compiledScript += sizeof(uint32_t);
	memcpy(compiledScript, inputChunk, sizeof(uint32_t) * inputWriteCount);
	compiledScript += sizeof(uint32_t) * inputWriteCount;
	memcpy(compiledScript, paramChunk, paramWriteSize);


	free(opChunk);
	free(opIdChunk);
	free(inputChunk);
	free(paramChunk);

	return scriptBuffer;
}

void cranp_test(void)
{
	// Plain old vm construction
	{
		MIST_PROFILE_BEGIN("cranp_test", "vm construct");

		unsigned long long bufferSize = cranp_vm_buffer_size(1 << 16, 10);
		void* buffer = malloc(bufferSize);
		cranp_vm_t* vm = cranp_vm_buffer_create(buffer, 1 << 16, 10);
		free(buffer);

		MIST_PROFILE_END("cranp_test", "vm construct");
	}

	// Alloc a single chunk and write to it
	{
		MIST_PROFILE_BEGIN("cranp_test", "vm chunk");

		unsigned long long bufferSize = cranp_vm_buffer_size(1 << 16, 10);
		void* buffer = malloc(bufferSize);
		cranp_vm_t* vm = cranp_vm_buffer_create(buffer, 1 << 16, 10);

		void* chunk = cranp_vm_alloc_chunk(vm, 0, 100);
		memset(chunk, 0, 1 << 16 / 10);
		free(buffer);

		MIST_PROFILE_END("cranp_test", "vm chunk");
	}

	// Parse the test_basic script and run it
	{
		MIST_PROFILE_BEGIN("cranp_test", "vm basic script");

		unsigned long long bufferSize = cranp_vm_buffer_size(1 << 20, 4);
		void* buffer = malloc(bufferSize);
		cranp_vm_t* vm = cranp_vm_buffer_create(buffer, 1 << 20, 4);

		cranp_script_t* script = cranp_test_parse_script(cranp_test_script_basic);
		cranp_init_script(script);

		MIST_PROFILE_BEGIN("cranp_test", "execute");
		cranp_vm_execute_script(vm, script);
		MIST_PROFILE_END("cranp_test", "execute");

		free(script);
		free(buffer);

		MIST_PROFILE_END("cranp_test", "vm basic script");
	}
}

#endif // CRANBERRY_PROCEDURAL_TESTS

#endif // CRANBERRY_PROCEDURAL_IMPLEMENTATION
