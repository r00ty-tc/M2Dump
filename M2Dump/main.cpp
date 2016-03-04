// M2 file dumper. Only currently supports cameras. Deal with it
// Also it doesn't bounds check the buffer. So corrupt files will cause a crash. Guess what? Deal with it!
#define _USE_MATH_DEFINES = true;
#include <math.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <iomanip>

using namespace std;
typedef uint64_t uint64;
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;

// Shamelessly taken from https://wowdev.wiki because laziness is good!
template<typename T>
struct M2SplineKey
{
    T p0;
    T p1;
    T p2;
};

struct C3Vector
{
    float x;
    float y;
    float z;
};

struct CAaBox
{
    C3Vector min;
    C3Vector max;
};

struct M2Header
{
    char   Magic[4];               // "MD20"
    uint32 Version;                // The version of the format.
    uint32 lName;                  // Length of the model's name including the trailing \0
    uint32 ofsName;                // Offset to the name, it seems like models can get reloaded by this name.should be unique, i guess.
    uint32 GlobalModelFlags;       // 0x0001: tilt x, 0x0002: tilt y, 0x0008: add 2 fields in header, 0x0020: load .phys data (MoP+), 0x0080: has _lod .skin files (MoP?+), 0x0100: is camera related.
    uint32 nGlobalSequences;
    uint32 ofsGlobalSequences;     // A list of timestamps.
    uint32 nAnimations;
    uint32 ofsAnimations;          // Information about the animations in the model.
    uint32 nAnimationLookup;
    uint32 ofsAnimationLookup;     // Mapping of global IDs to the entries in the Animation sequences block.
    uint32 nBones;                 // MAX_BONES = 0x100
    uint32 ofsBones;               // Information about the bones in this model.
    uint32 nKeyBoneLookup;
    uint32 ofsKeyBoneLookup;       // Lookup table for key skeletal bones.
    uint32 nVertices;
    uint32 ofsVertices;            // Vertices of the model.
    uint32 nViews;                 // Views (LOD) are now in .skins.
    uint32 nSubmeshAnimations;
    uint32 ofsSubmeshAnimations;   // Submesh color and alpha animations definitions.
    uint32 nTextures;
    uint32 ofsTextures;            // Textures of this model.
    uint32 nTransparency;
    uint32 ofsTransparency;        // Transparency of textures.
    uint32 nUVAnimation;
    uint32 ofsUVAnimation;
    uint32 nTexReplace;
    uint32 ofsTexReplace;          // Replaceable Textures.
    uint32 nRenderFlags;
    uint32 ofsRenderFlags;         // Blending modes / render flags.
    uint32 nBoneLookupTable;
    uint32 ofsBoneLookupTable;     // A bone lookup table.
    uint32 nTexLookup;
    uint32 ofsTexLookup;           // The same for textures.
    uint32 nTexUnits;              // possibly removed with cata?!
    uint32 ofsTexUnits;            // And texture units. Somewhere they have to be too.
    uint32 nTransLookup;
    uint32 ofsTransLookup;         // Everything needs its lookup. Here are the transparencies.
    uint32 nUVAnimLookup;
    uint32 ofsUVAnimLookup;
    CAaBox BoundingBox;            // min/max( [1].z, 2.0277779f ) - 0.16f seems to be the maximum camera height
    float  BoundingSphereRadius;
    CAaBox CollisionBox;
    float  CollisionSphereRadius;
    uint32 nBoundingTriangles;
    uint32 ofsBoundingTriangles;   // Our bounding volumes. Similar structure like in the old ofsViews.
    uint32 nBoundingVertices;
    uint32 ofsBoundingVertices;
    uint32 nBoundingNormals;
    uint32 ofsBoundingNormals;
    uint32 nAttachments;
    uint32 ofsAttachments;         // Attachments are for weapons etc.
    uint32 nAttachLookup;
    uint32 ofsAttachLookup;        // Of course with a lookup.
    uint32 nEvents;
    uint32 ofsEvents;              // Used for playing sounds when dying and a lot else.
    uint32 nLights;
    uint32 ofsLights;              // Lights are mainly used in loginscreens but in wands and some doodads too.
    uint32 nCameras;               // Format of Cameras changed with version 271!
    uint32 ofsCameras;             // The cameras are present in most models for having a model in the Character-Tab.
    uint32 nCameraLookup;
    uint32 ofsCameraLookup;        // And lookup-time again.
    uint32 nRibbonEmitters;
    uint32 ofsRibbonEmitters;      // Things swirling around. See the CoT-entrance for light-trails.
    uint32 nParticleEmitters;
    uint32 ofsParticleEmitters;    // Spells and weapons, doodads and loginscreens use them. Blood dripping of a blade? Particles.
    uint32 nBlendMaps;             // This has to deal with blending. Exists IFF (flags & 0x8) != 0. When set, textures blending is overriden by the associated array. See M2/WotLK#Blend_mode_overrides
    uint32 ofsBlendMaps;           // Same as above. Points to an array of uint16 of nBlendMaps entries -- From WoD information.};
};

struct M2Array
{
    uint32_t number;
    uint32 offset_elements;
};
struct M2Track
{
    uint16_t interpolation_type;
    uint16_t global_sequence;
    M2Array timestamps;
    M2Array values;
};

struct M2Camera
{
    uint32_t type; // 0: portrait, 1: characterinfo; -1: else (flyby etc.); referenced backwards in the lookup table.
    float fov; // No radians, no degrees. Multiply by 35 to get degrees.
    float far_clip;
    float near_clip;
    M2Track positions; // How the camera's position moves. Should be 3*3 floats.
    C3Vector position_base;
    M2Track target_positions; // How the target moves. Should be 3*3 floats.
    C3Vector target_position_base;
    M2Track rolldata; // The camera can have some roll-effect. Its 0 to 2*Pi. 
};

void dumpCameras(M2Camera const* cam, uint32 numCams, char const* buffer);

int main(int argc, char* argv[])
{
    // Sanity check arguments
    if (argc != 2)
    {
        cout << "Usage: M2Dump <filename>\n";
        return 1;
    }

    // Open file
    char* filename = argv[1];
    std::ifstream fin(filename, ios::in | ios::binary);

    // Check it opened
    if (!fin.is_open())
    {
        cout << "File " << filename << " was not opened\n";
        return 1;
    }

    // Get file size
    fin.seekg(0, ios::end);
    std::streamoff const fileSize = fin.tellg();

    // Reject if not at least 4 bytes
    if (fileSize < 4)
    {
        cout << "File " << filename << " invalid, too short\n";
        return 1;
    }

    // Read 4 bytes (signature)
    fin.seekg(0, ios::beg);
    char fileCheck[5];
    fin.read(fileCheck, 4);
    fileCheck[4] = 0;

    // Check file has correct magic (MD20)
    if (strcmp(fileCheck, "MD20"))
    {
        cout << "File " << filename << " invalid, bad signature\n";
        return 1;
    }

    // Now we have a good file, read it all into a vector of char's, then close the file.
    std::vector<char> buffer(fileSize);
    fin.seekg(0, ios::beg);
    if (!fin.read(buffer.data(), fileSize))
    {
        cout << "Failed to read file " << filename << " error during read operation\n";
        return 1;
    }
    fin.close();

    // Read header
    M2Header const* header = (M2Header const*)(buffer.data());

    // Get the name, from the location references at 0x0C
    char const* name = (char const*)(buffer.data() + header->ofsName);
    cout << "M2 Name is: " << name << "\n";

    // Get camera(s) - Main header, then dump them.
    M2Camera const* cam = (M2Camera const*)(buffer.data() + header->ofsCameras);
    dumpCameras(cam, header->nCameras, buffer.data());

    return 0;
}

void dumpCameras(M2Camera const* cam, uint32 numCams, char const* buffer)
{
    cout << std::fixed << std::setprecision(6);
    cout << "Found " << numCams << " camera(s)\n";
    for (uint32 j = 0; j < numCams; ++j)
    {
        // Output header info for this camera (usually just one camera, but still)
        cout << "  Handling camera " << j + 1 << " of " << cam->positions.timestamps.number << "\n";
        cout << "  Camera FOV " << cam->fov << " interpolation type " << cam->positions.interpolation_type << "\n";
        cout << "  Camera Near clip " << cam->near_clip << " far clip " << cam->far_clip << "\n";
        cout << "  Start position       [" << std::setw(12) << cam->position_base.x << ", " << std::setw(12) << cam->position_base.y << ", " << std::setw(12) << cam->position_base.z << "]\n";
        cout << "  End position         [" << std::setw(12) << cam->target_position_base.x << ", " << std::setw(12) << cam->target_position_base.y << ", " << std::setw(12) << cam->target_position_base.z << "]\n\n";

        // Dump camera positions and timestamps
        cout << "  Position Data:\n";
        for (uint32 k = 0; k < cam->positions.timestamps.number; ++k)
        {
            // Usually only 1, but we should support more (maybe cata uses?)
            cout << "    Position Set 1 of " << cam->positions.timestamps.number << "\n";

            // Extract Camera positions for this set
            M2Array const* posTsArray = (M2Array const*)(buffer + cam->positions.timestamps.offset_elements);
            uint32 const* posTimestamps = (uint32 const*)(buffer + posTsArray->offset_elements);
            M2Array const* posArray = (M2Array const*)(buffer + cam->positions.values.offset_elements);
            M2SplineKey<C3Vector> const* positions = (M2SplineKey<C3Vector> const*)(buffer + posArray->offset_elements);

            // Dump the data for this set
            for (uint32 i = 0; i < posTsArray->number; ++i)
            {
                cout << "      Timestamp " << i << ": " << posTimestamps[i] << "\n";
                cout << "        Position Data 1: [" << std::setw(12) << positions->p0.x << ", " << std::setw(12) << positions->p0.y << ", " << std::setw(12) << positions->p0.z << "]\n";
                cout << "        Position Data 2: [" << std::setw(12) << positions->p1.x << ", " << std::setw(12) << positions->p1.y << ", " << std::setw(12) << positions->p1.z << "]\n";
                cout << "        Position Data 3: [" << std::setw(12) << positions->p2.x << ", " << std::setw(12) << positions->p2.y << ", " << std::setw(12) << positions->p2.z << "]\n";
                positions++;
            }
            posTsArray++;
        }

        // Dump camera target positions and timestamps
        cout << "  Target Position Data:\n";
        for (uint32 k = 0; k < cam->target_positions.timestamps.number; ++k)
        {
            // Usually only 1, but we should support more (maybe cata uses?)
            cout << "    Target Position Set 1 of " << cam->target_positions.timestamps.number << "\n";

            // Extract Target positions
            M2Array const* targTsArray = (M2Array const*)(buffer + cam->target_positions.timestamps.offset_elements);
            uint32 const* targTimestamps = (uint32 const*)(buffer + targTsArray->offset_elements);
            M2Array const* targArray = (M2Array const*)(buffer + cam->target_positions.values.offset_elements);
            M2SplineKey<C3Vector> const* targPositions = (M2SplineKey<C3Vector> const*)(buffer + targArray->offset_elements);

            // Dump the data for this set
            for (uint32 i = 0; i < targTsArray->number; ++i)
            {
                cout << "      Timestamp " << i << ": " << targTimestamps[i] << "\n";
                cout << "        Target Data 1:   [" << std::setw(12) << targPositions->p0.x << ", " << std::setw(12) << targPositions->p0.y << ", " << std::setw(12) << targPositions->p0.z << "]\n";
                cout << "        Target Data 2:   [" << std::setw(12) << targPositions->p1.x << ", " << std::setw(12) << targPositions->p1.y << ", " << std::setw(12) << targPositions->p1.z << "]\n";
                cout << "        Target Data 3:   [" << std::setw(12) << targPositions->p2.x << ", " << std::setw(12) << targPositions->p2.y << ", " << std::setw(12) << targPositions->p2.z << "]\n";
                targPositions++;
            }
            targTsArray++;
        }

        // Dump roll data and timestamps
        cout << "  Roll Data:\n";
        for (uint32 k = 0; k < cam->rolldata.timestamps.number; ++k)
        {
            // Usually only 1, but we should support more (maybe cata uses?)
            cout << "    Roll Set 1 of " << cam->rolldata.timestamps.number << "\n";

            // Extract roll positions
            M2Array const* rollTsArray = (M2Array const*)(buffer + cam->rolldata.timestamps.offset_elements);
            uint32 const* rollTimestamps = (uint32 const*)(buffer + rollTsArray->offset_elements);
            M2Array const* rollArray = (M2Array const*)(buffer + cam->rolldata.values.offset_elements);
            M2SplineKey<float> const* rollPositions = (M2SplineKey<float> const*)(buffer + rollArray->offset_elements);

            // Dump the data for this set
            for (uint32 i = 0; i < rollTsArray->number; ++i)
            {
                cout << "      Timestamp " << i << ": " << rollTimestamps[i] << "\n";
                cout << "        Roll Data 1: " << std::setw(12) << rollPositions->p0 << " (" << std::fmod(rollPositions->p0 * (180 / M_PI), 360.0f) << " degrees) \n";
                cout << "        Roll Data 2: " << std::setw(12) << rollPositions->p1 << " (" << std::fmod(rollPositions->p1 * (180 / M_PI), 360.0f) << " degrees) \n";
                cout << "        Roll Data 3: " << std::setw(12) << rollPositions->p2 << " (" << std::fmod(rollPositions->p2 * (180 / M_PI), 360.0f) << " degrees) \n";
                rollPositions++;
            }
            rollTsArray++;
        }

        cam++;
    }

}