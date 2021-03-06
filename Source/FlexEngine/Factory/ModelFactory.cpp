#include <FlexEngine/Factory/ModelFactory.h>

#include <FlexEngine/Math/MathDefs.h>

#include <Urho3D/AngelScript/ScriptFile.h>
#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/Geometry.h>
#include <Urho3D/Graphics/IndexBuffer.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/VertexBuffer.h>
#include <Urho3D/Resource/XMLElement.h>

namespace FlexEngine
{

void AdjustIndicesBase(unsigned char* indexData, unsigned indexDataSize, bool largeIndices, unsigned baseIndex)
{
    const unsigned numIndices = indexDataSize / (largeIndices ? 4 : 2);
    if (largeIndices)
    {
        AdjustIndicesBase(reinterpret_cast<unsigned*>(indexData), numIndices, baseIndex);
    }
    else
    {
        AdjustIndicesBase(reinterpret_cast<unsigned short*>(indexData), numIndices, baseIndex);
    }
}

//////////////////////////////////////////////////////////////////////////
const PODVector<VertexElement>& DefaultVertex::GetVertexElements()
{
    static_assert(MAX_VERTEX_BONES == 4, "Update vertex elements!");
    static_assert(MAX_VERTEX_TEXCOORD == 4, "Update vertex elements!");
    static const PODVector<VertexElement> elements =
    {
        VertexElement(TYPE_VECTOR3, SEM_POSITION),
        VertexElement(TYPE_VECTOR3, SEM_TANGENT),
        VertexElement(TYPE_VECTOR3, SEM_BINORMAL),
        VertexElement(TYPE_VECTOR3, SEM_NORMAL, 0),
        VertexElement(TYPE_VECTOR3, SEM_NORMAL, 1),
        VertexElement(TYPE_VECTOR4, SEM_TEXCOORD, 0),
        VertexElement(TYPE_VECTOR4, SEM_TEXCOORD, 1),
        VertexElement(TYPE_VECTOR4, SEM_TEXCOORD, 2),
        VertexElement(TYPE_VECTOR4, SEM_TEXCOORD, 3),
        VertexElement(TYPE_VECTOR4, SEM_COLOR, 0),
        VertexElement(TYPE_VECTOR4, SEM_COLOR, 1),
        VertexElement(TYPE_VECTOR4, SEM_COLOR, 2),
        VertexElement(TYPE_VECTOR4, SEM_COLOR, 3),
        VertexElement(TYPE_UBYTE4,  SEM_BLENDINDICES),
        VertexElement(TYPE_VECTOR4, SEM_BLENDWEIGHTS),
    };
    return elements;
}

Vector4 DefaultVertex::GetPackedTangentBinormal() const
{
    return Vector4(tangent_, CrossProduct(tangent_, normal_).DotProduct(binormal_) > 0 ? 1.0f : -1.0f);
}

DefaultVertex LerpVertices(const DefaultVertex& lhs, const DefaultVertex& rhs, float factor)
{
    DefaultVertex result;

    result.position_ = Lerp(lhs.position_, rhs.position_, factor);
    result.geometryNormal_ = Lerp(lhs.geometryNormal_, rhs.geometryNormal_, factor).Normalized();
    result.tangent_ = Lerp(lhs.tangent_, rhs.tangent_, factor).Normalized();
    result.binormal_ = Lerp(lhs.binormal_, rhs.binormal_, factor).Normalized();
    result.normal_ = Lerp(lhs.normal_, rhs.normal_, factor).Normalized();
    for (unsigned i = 0; i < MAX_VERTEX_TEXCOORD; ++i)
    {
        result.uv_[i] = Lerp(lhs.uv_[i], rhs.uv_[i], factor);
    }
    for (unsigned i = 0; i < MAX_VERTEX_COLOR; ++i)
    {
        result.colors_[i] = Lerp(lhs.colors_[i], rhs.colors_[i], factor);
    }
    for (unsigned i = 0; i < MAX_VERTEX_BONES; ++i)
    {
        result.boneIndices_[i] = lhs.boneIndices_[i];
        result.boneWeights_[i] = Lerp(lhs.boneWeights_[i], rhs.boneWeights_[i], factor);
    }

    return result;
}

DefaultVertex QLerpVertices(const DefaultVertex& v0, const DefaultVertex& v1, const DefaultVertex& v2, const DefaultVertex& v3,
    const float factor1, const float factor2)
{
    return LerpVertices(LerpVertices(v0, v1, factor1), LerpVertices(v2, v3, factor1), factor2);
}

//////////////////////////////////////////////////////////////////////////
ModelFactory::ModelFactory(Context* context)
    : Object(context)
{

}

ModelFactory::~ModelFactory()
{

}

void ModelFactory::Reset()
{
    vertexElements_.Clear();
    vertexSize_ = 0;
    largeIndices_ = false;
    currentGeometry_ = 0;
    currentLevel_ = 0;
    geometry_.Clear();
}

void ModelFactory::Initialize(const PODVector<VertexElement>& vertexElements, bool largeIndices)
{
    Reset();

    vertexElements_ = vertexElements;
    largeIndices_ = largeIndices;

    // Use temporary buffer to compute vertex stride
    VertexBuffer buffer(context_);
    buffer.SetSize(0, vertexElements_);
    vertexSize_ = buffer.GetVertexSize();
}

void ModelFactory::SetLevel(unsigned level)
{
    currentLevel_ = level;
}

void ModelFactory::AddGeometry(SharedPtr<Material> material, bool allowReuse /*= true*/)
{
    // Find existing
    const Vector<SharedPtr<Material>>::Iterator it = materials_.Find(material);

    // Add new
    if (it == materials_.End() || !allowReuse)
    {
        currentGeometry_ = materials_.Size();
        materials_.Push(material);
        geometry_.Push(Vector<ModelGeometryBuffer>());
    }
    else
    {
        currentGeometry_ = it - materials_.Begin();
    }
}

void ModelFactory::AddEmpty()
{
    const unsigned numGeometries = geometry_.Size();
    if (currentGeometry_ >= numGeometries)
    {
        geometry_.Resize(numGeometries + 1);
    }

    const unsigned numLevels = geometry_[currentGeometry_].Size();
    if (currentLevel_ >= numLevels)
    {
        geometry_[currentGeometry_].Resize(numLevels + 1);
    }
}

void ModelFactory::AddPrimitives(const void* vertexData, unsigned numVertices, const void* indexData, unsigned numIndices, bool adjustIndices)
{
    // Get destination buffers
    AddEmpty();
    ModelGeometryBuffer& geometryBuffer = geometry_[currentGeometry_][currentLevel_];

    // Copy vertex data
    geometryBuffer.vertexData.Insert(geometryBuffer.vertexData.End(),
        static_cast<const unsigned char*>(vertexData),
        static_cast<const unsigned char*>(vertexData) + numVertices * GetVertexSize());

    // Copy index data
    geometryBuffer.indexData.Insert(geometryBuffer.indexData.End(),
        static_cast<const unsigned char*>(indexData),
        static_cast<const unsigned char*>(indexData) + numIndices * GetIndexSize());

    // Adjust indices
    if (adjustIndices)
    {
        const unsigned base = geometryBuffer.vertexData.Size() / GetVertexSize() - numVertices;
        const unsigned offset = geometryBuffer.indexData.Size() - numIndices * GetIndexSize();
        AdjustIndicesBase(geometryBuffer.indexData.Buffer() + offset, numIndices * GetIndexSize(), largeIndices_, base);
    }
}

unsigned ModelFactory::GetCurrentNumVertices() const
{
    return GetNumVertices(currentGeometry_, currentLevel_);
}

unsigned ModelFactory::GetNumGeometries() const
{
    return geometry_.Size();
}

unsigned ModelFactory::GetNumGeometryLevels(unsigned geometry) const
{
    return geometry < GetNumGeometries() ? geometry_[geometry].Size() : 0;
}

unsigned ModelFactory::GetNumVertices(unsigned geometry, unsigned level) const
{
    return level < GetNumGeometryLevels(geometry) ? geometry_[geometry][level].vertexData.Size() / GetVertexSize() : 0;
}

unsigned ModelFactory::GetNumIndices(unsigned geometry, unsigned level) const
{
    return level < GetNumGeometryLevels(geometry) ? geometry_[geometry][level].indexData.Size() / GetIndexSize() : 0;
}

const void* ModelFactory::GetVertices(unsigned geometry, unsigned level) const
{
    return level < GetNumGeometryLevels(geometry) ? geometry_[geometry][level].vertexData.Buffer() : nullptr;
}

const void* ModelFactory::GetIndices(unsigned geometry, unsigned level) const
{
    return level < GetNumGeometryLevels(geometry) ? geometry_[geometry][level].indexData.Buffer() : nullptr;
}

const Vector<SharedPtr<Material>>& ModelFactory::GetMaterials() const
{
    return materials_;
}

SharedPtr<Model> ModelFactory::BuildModel() const
{
    // Filter geometries without LODs
    Vector<Vector<ModelGeometryBuffer>> geometry;
    for (unsigned i = 0; i < geometry_.Size(); ++i)
        if (geometry_[i].Size() > 0)
            geometry.Push(geometry_[i]);

    // Prepare buffers for accumulated geometry data
    SharedPtr<VertexBuffer> vertexBuffer = MakeShared<VertexBuffer>(context_);
    vertexBuffer->SetShadowed(true);

    SharedPtr<IndexBuffer> indexBuffer = MakeShared<IndexBuffer>(context_);
    indexBuffer->SetShadowed(true);

    SharedPtr<Model> model = MakeShared<Model>(context_);
    model->SetVertexBuffers({ vertexBuffer }, { 0 }, { 0 });
    model->SetIndexBuffers({ indexBuffer });

    // Number of geometries is equal to number of materials
    model->SetNumGeometries(geometry.Size());
    for (unsigned i = 0; i < geometry.Size(); ++i)
    {
        model->SetNumGeometryLodLevels(i, geometry[i].Size());
    }

    // Merge all arrays into one
    PODVector<unsigned char> vertexData;
    PODVector<unsigned char> indexData;
    PODVector<unsigned> geometryIndexOffset;
    PODVector<unsigned> geometryIndexCount;

    for (unsigned i = 0; i < geometry.Size(); ++i)
    {
        for (unsigned j = 0; j < geometry[i].Size(); ++j)
        {
            const ModelGeometryBuffer& geometryBuffer = geometry[i][j];

            // Merge buffers
            geometryIndexOffset.Push(indexData.Size() / GetIndexSize());
            vertexData += geometryBuffer.vertexData;
            indexData += geometryBuffer.indexData;
            geometryIndexCount.Push(geometryBuffer.indexData.Size() / GetIndexSize());

            // Adjust indices
            const unsigned base = (vertexData.Size() - geometryBuffer.vertexData.Size()) / GetVertexSize();
            const unsigned offset = indexData.Size() - geometryBuffer.indexData.Size();
            AdjustIndicesBase(indexData.Buffer() + offset, geometryBuffer.indexData.Size(), largeIndices_, base);

            // Create geometry
            SharedPtr<Geometry> geometry = MakeShared<Geometry>(context_);
            geometry->SetVertexBuffer(0, vertexBuffer);
            geometry->SetIndexBuffer(indexBuffer);
            model->SetGeometry(i, j, geometry);
        }
    }

    // Flush data to buffers
    vertexBuffer->SetSize(vertexData.Size() / GetVertexSize(), vertexElements_);
    vertexBuffer->SetData(vertexData.Buffer());
    indexBuffer->SetSize(indexData.Size() / GetIndexSize(), largeIndices_);
    indexBuffer->SetData(indexData.Buffer());

    // Setup ranges
    unsigned group = 0;
    for (unsigned i = 0; i < geometry.Size(); ++i)
    {
        for (unsigned lod = 0; lod < geometry[i].Size(); ++lod)
        {
            model->GetGeometry(i, lod)->SetDrawRange(TRIANGLE_LIST, geometryIndexOffset[group], geometryIndexCount[group]);
            ++group;
        }
    }

    // Try to compute bounding box.
    int positionOffset = -1;
    for (const VertexElement& element : vertexBuffer->GetElements())
    {
        if (element.semantic_ == SEM_POSITION && element.index_ == 0)
        {
            if (element.type_ != TYPE_VECTOR3 && element.type_ != TYPE_VECTOR4)
            {
                URHO3D_LOGERROR("Position attribute must have type Vector3 or Vector4");
            }
            else
            {
                positionOffset = element.offset_;
            }
            break;
        }
    }
    if (positionOffset < 0)
    {
        URHO3D_LOGERROR("Position was not found");
    }
    else
    {
        const unsigned char* data = vertexBuffer->GetShadowData();
        BoundingBox boundingBox;
        for (unsigned i = 0; i < vertexBuffer->GetVertexCount(); ++i)
        {
            boundingBox.Merge(*reinterpret_cast<const Vector3*>(data + positionOffset + vertexSize_ * i));
        }
        model->SetBoundingBox(boundingBox);
    }

    return model;
}

//////////////////////////////////////////////////////////////////////////
SharedPtr<ModelFactory> CreateModelFromScript(ScriptFile& scriptFile, const String& entryPoint)
{
    SharedPtr<ModelFactory> factory = MakeShared<ModelFactory>(scriptFile.GetContext());
    factory->Initialize(DefaultVertex::GetVertexElements(), true);

    const VariantVector param = { Variant(factory) };
    if (!scriptFile.Execute(ToString("void %s(ModelFactory@ dest)", entryPoint.CString()), param))
    {
        return nullptr;
    }

    return factory;
}

SharedPtr<Model> CreateQuadModel(Context* context)
{
    static const unsigned indices[6] = { 0, 2, 3, 0, 3, 1 };
    static const Vector2 positions[4] = { Vector2(0, 0), Vector2(1, 0), Vector2(0, 1), Vector2(1, 1) };
    DefaultVertex vertices[4];
    for (unsigned i = 0; i < 4; ++i)
    {
        vertices[i].position_ = Vector3(positions[i], 0.5f);
        vertices[i].uv_[0] = Vector4(positions[i].x_, 1.0f - positions[i].y_, 0.0f, 0.0f);
        vertices[i].uv_[1] = Vector4::ONE;
    }

    ModelFactory factory(context);
    factory.Initialize(DefaultVertex::GetVertexElements(), true);
    factory.AddPrimitives(vertices, indices, true);
    return factory.BuildModel();
}

SharedPtr<Model> GetOrCreateQuadModel(Context* context)
{
    static const String modelName = "DefaultRenderTargetModel";
    const Variant& var = context->GetGlobalVar(modelName);

    // Get existing
    if (var.GetType() == VAR_PTR)
    {
        if (Object* object = dynamic_cast<Object*>(var.GetPtr()))
        {
            if (Model* model = dynamic_cast<Model*>(object))
            {
                return SharedPtr<Model>(model);
            }
        }
    }

    // Create new
    const SharedPtr<Model> model = CreateQuadModel(context);
    context->SetGlobalVar(modelName, var);
    return model;
}

void AppendModelGeometries(Model& dest, const Model& source)
{
    const unsigned numGeometries = dest.GetNumGeometries();

    // Append vertex buffers
    Vector<SharedPtr<VertexBuffer>> vertexBuffers = dest.GetVertexBuffers();
    vertexBuffers += source.GetVertexBuffers();

    PODVector<unsigned> morphRangeStarts;
    PODVector<unsigned> morphRangeCounts;
    for (unsigned i = 0; i < dest.GetVertexBuffers().Size(); ++i)
    {
        morphRangeStarts.Push(dest.GetMorphRangeStart(i));
        morphRangeCounts.Push(dest.GetMorphRangeCount(i));
    }
    for (unsigned i = 0; i < source.GetVertexBuffers().Size(); ++i)
    {
        morphRangeStarts.Push(source.GetMorphRangeStart(i));
        morphRangeCounts.Push(source.GetMorphRangeCount(i));
    }
    dest.SetVertexBuffers(vertexBuffers, morphRangeStarts, morphRangeCounts);

    // Append index buffers
    Vector<SharedPtr<IndexBuffer>> indexBuffers = dest.GetIndexBuffers();
    indexBuffers += source.GetIndexBuffers();
    dest.SetIndexBuffers(indexBuffers);

    // Append geometries
    dest.SetNumGeometries(numGeometries + source.GetNumGeometries());
    for (unsigned i = 0; i < source.GetNumGeometries(); ++i)
    {
        dest.SetNumGeometryLodLevels(numGeometries + i, source.GetNumGeometryLodLevels(i));
        for (unsigned j = 0; j < source.GetNumGeometryLodLevels(i); ++j)
        {
            dest.SetGeometry(numGeometries + i, j, source.GetGeometry(i, j));
        }
    }
}

void AppendEmptyLOD(Model& model, float distance)
{
    for (unsigned i = 0; i < model.GetNumGeometries(); ++i)
    {
        const unsigned num = model.GetNumGeometryLodLevels(i);
        model.SetNumGeometryLodLevels(i, num + 1);

        SharedPtr<Geometry> geometry = MakeShared<Geometry>(model.GetContext());
        geometry->SetLodDistance(distance);
        model.SetGeometry(i, num, geometry);
    }
}

}
