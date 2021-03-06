#include <FlexEngine/Factory/TextureFactory.h>

#include <FlexEngine/Factory/FactoryContext.h>
#include <FlexEngine/Factory/ModelFactory.h>
#include <FlexEngine/Factory/ProxyGeometryFactory.h>
#include <FlexEngine/Resource/ResourceCacheHelpers.h>
#include <FlexEngine/Resource/XMLHelpers.h>

#include <Urho3D/AngelScript/ScriptFile.h>
#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/View.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/Scene/Scene.h>

namespace FlexEngine
{

namespace
{

/// Get texture unit by name.
TextureUnit ParseTextureUnit(const String& name)
{
    const String str = name.ToLower().Trimmed();
    if (str == "diffuse" || str == "diff" || str == "0")
    {
        return TU_DIFFUSE;
    }
    else if(str == "normal" || str == "norm" || str == "1")
    {
        return TU_NORMAL;
    }
    else if (str == "specular" || str == "spec" || str == "2")
    {
        return TU_SPECULAR;
    }
    else if (str == "emissive" || str == "3")
    {
        return TU_EMISSIVE;
    }
    else
    {
        return MAX_TEXTURE_UNITS;
    }
}

}

const char* inputParameterUniform[MAX_INPUT_UNIFORM_PARAMETERS] = { "MatDiffColor" };

SharedPtr<Texture2D> RenderViews(Context* context, unsigned width, unsigned height, const Vector<ViewDescription>& views)
{
    // Allocate destination buffers
    SharedPtr<Texture2D> texture = MakeShared<Texture2D>(context);
    texture->SetSize(width, height, Graphics::GetRGBAFormat(), TEXTURE_RENDERTARGET);
    SharedPtr<RenderSurface> renderSurface(texture->GetRenderSurface());

    // Prepare views
    Graphics* graphics = context->GetSubsystem<Graphics>();
    if (graphics->BeginFrame())
    {
        for (const ViewDescription& desc : views)
        {
            // Construct scene
            SharedPtr<Scene> scene(new Scene(context));
            Octree* octree = scene->CreateComponent<Octree>();
            Zone* zone = scene->CreateComponent<Zone>();
            zone->SetAmbientColor(Color(1.0f, 1.0f, 1.0f));
            zone->SetFogColor(Color::TRANSPARENT);
            zone->SetBoundingBox(BoundingBox(
                Vector3(-M_LARGE_VALUE, -M_LARGE_VALUE, -M_LARGE_VALUE), Vector3(M_LARGE_VALUE, M_LARGE_VALUE, M_LARGE_VALUE)));
            scene->AddChild(desc.node_);
            scene->AddChild(desc.camera_);

            // Get camera
            Camera* camera = desc.camera_->GetComponent<Camera>();
            if (!camera)
            {
                URHO3D_LOGERROR("Camera node must contain camera component");
                continue;
            }

            // Setup viewport
            Viewport viewport(context);
            viewport.SetCamera(camera);
            viewport.SetRect(desc.viewport_);
            viewport.SetRenderPath(desc.renderPath_);
            viewport.SetScene(scene);

            // Render scene
            View view(context);
            view.Define(renderSurface, &viewport);
            view.Update(FrameInfo());
            view.Render();

            scene->RemoveChild(desc.node_);
            scene->RemoveChild(desc.camera_);
        }
        graphics->EndFrame();
    }

    return texture;
}

SharedPtr<Image> ConvertTextureToImage(const Texture2D* texture)
{
    if (!texture)
    {
        URHO3D_LOGERROR("Texture mustn't be null");
        return nullptr;
    }

    SharedPtr<Image> image = texture->GetImage();
    image->SetName(texture->GetName());
    return image;
}

SharedPtr<Texture2D> ConvertImageToTexture(const Image* image)
{
    if (!image)
    {
        URHO3D_LOGERROR("Image mustn't be null");
        return nullptr;
    }

    SharedPtr<Texture2D> texture = MakeShared<Texture2D>(image->GetContext());
    texture->SetData(const_cast<Image*>(image));
    return texture;
}

SharedPtr<Image> ConvertColorKeyToAlpha(SharedPtr<Image> image, const Color& colorKey)
{
    if (!image)
    {
        URHO3D_LOGERROR("Image mustn't be null");
        return nullptr;
    }

    SharedPtr<Image> transparentImage = MakeShared<Image>(image->GetContext());
    transparentImage->SetSize(image->GetWidth(), image->GetHeight(), image->GetDepth(), 4);
    for (int y = 0; y < image->GetHeight(); ++y)
    {
        for (int x = 0; x < image->GetWidth(); ++x)
        {
            const Color color = image->GetPixel(x, y);
            if (Abs((colorKey - color).Luma()) < M_LARGE_EPSILON)
            {
                transparentImage->SetPixel(x, y, Color::TRANSPARENT);
            }
            else
            {
                transparentImage->SetPixel(x, y, Color(color, 1.0f));
            }
        }
    }
    return transparentImage;
}

void CopyImageAlpha(SharedPtr<Image> destImage, SharedPtr<Image> sourceAlpha)
{
    for (int y = 0; y < destImage->GetHeight(); ++y)
    {
        for (int x = 0; x < destImage->GetWidth(); ++x)
        {
            destImage->SetPixel(x, y, Color(destImage->GetPixel(x, y), sourceAlpha->GetPixel(x, y).a_));
        }
    }
}

void ResetImageAlpha(SharedPtr<Image> image, float alpha /*= 1.0f*/)
{
    for (int y = 0; y < image->GetHeight(); ++y)
    {
        for (int x = 0; x < image->GetWidth(); ++x)
        {
            image->SetPixel(x, y, Color(image->GetPixel(x, y), alpha));
        }
    }
}

unsigned GetNumImageLevels(const Image& image)
{
    return Texture::CheckMaxLevels(image.GetWidth(), image.GetHeight(), 0);
}

void AdjustImageLevelsAlpha(Image& image, float factor)
{
    const unsigned numLevels = GetNumImageLevels(image);
    if (numLevels <= 1)
    {
        return;
    }

    SharedPtr<Image> level = image.GetNextLevel();
    float k = factor;
    for (unsigned i = 1; i < numLevels; ++i)
    {
        for (int y = 0; y < level->GetHeight(); ++y)
        {
            for (int x = 0; x < level->GetWidth(); ++x)
            {
                Color color = level->GetPixel(x, y);
                color.a_ *= k;
                level->SetPixel(x, y, color);
            }
        }
        k *= factor;
        level = level->GetNextLevel();
    }
}

void FlipNormalMapZ(Image& image)
{
    for (int y = 0; y < image.GetHeight(); ++y)
        for (int x = 0; x < image.GetWidth(); ++x)
        {
            Color color = image.GetPixel(x, y);
            if (color.Luma() > M_LARGE_EPSILON)
            {
                color.b_ = 1.0f - color.b_;
                image.SetPixel(x, y, color);
            }
        }
}

OrthoCameraDescription OrthoCameraDescription::Identity(unsigned width, unsigned height, const Vector3& offset /*= Vector3::ZERO*/)
{
    OrthoCameraDescription result;
    result.position_ = Vector3(0.5f, 0.5f, 0.0f) + offset;
    result.farClip_ = 1.0f;
    result.size_ = Vector2(1.0f, 1.0f);
    result.viewport_ = IntRect(0, 0, width, height);
    return result;
}

Vector<ViewDescription> ConstructViewsForTexture(Context* context, const TextureDescription& desc, const TextureMap& textures)
{
    ResourceCache* resourceCache = context->GetSubsystem<ResourceCache>();
    Vector<ViewDescription> views;

    for (const OrthoCameraDescription& cameraDesc : desc.cameras_)
    {
        ViewDescription viewDesc;

        // Create camera node
        viewDesc.camera_ = MakeShared<Node>(context);
        viewDesc.camera_->SetPosition(cameraDesc.position_);
        viewDesc.camera_->SetRotation(cameraDesc.rotation_);

        // Setup camera
        Camera* camera = viewDesc.camera_->CreateComponent<Camera>();
        if (camera)
        {
            camera->SetOrthographic(true);
            camera->SetFarClip(cameraDesc.farClip_);
            camera->SetOrthoSize(cameraDesc.size_);
        }

        // Create model node
        viewDesc.node_ = MakeShared<Node>(context);

        // Setup geometry
        for (const GeometryDescription& geometryDesc : desc.geometries_)
        {
            StaticModel* staticModel = viewDesc.node_->CreateComponent<StaticModel>();
            if (staticModel)
            {
                staticModel->SetModel(geometryDesc.model_);
                for (unsigned i = 0; i < geometryDesc.materials_.Size(); ++i)
                {
                    if (!geometryDesc.materials_[i])
                    {
                        URHO3D_LOGERROR("Missing material of source model");
                        continue;
                    }

                    // Clone material
                    const SharedPtr<Material> material(geometryDesc.materials_[i]->Clone());

                    // Override textures
                    for (const HashMap<TextureUnit, String>::KeyValue& unitDesc : desc.textures_)
                    {
                        SharedPtr<Texture2D> texture;
                        textures.TryGetValue(unitDesc.second_, texture);
                        if (!texture && resourceCache)
                        {
                            texture = resourceCache->GetResource<Texture2D>(unitDesc.second_);
                        }
                        if (texture)
                        {
                            material->SetTexture(unitDesc.first_, texture);
                        }
                        else
                        {
                            URHO3D_LOGERRORF("Cannot resolve input texture name '%s'", unitDesc.second_.CString());
                        }
                    }

                    // Override parameters
                    for (const HashMap<String, Variant>::KeyValue& paramDesc : desc.parameters_)
                    {
                        material->SetShaderParameter(paramDesc.first_, paramDesc.second_);
                    }

                    staticModel->SetMaterial(i, material);
                    viewDesc.objects_.Push(material);
                }
            }
        }

        // Setup other fields
        viewDesc.viewport_ = cameraDesc.viewport_;
        viewDesc.renderPath_ = desc.renderPath_;

        views.Push(viewDesc);
    }

    return views;
}

SharedPtr<Texture2D> RenderTexture(Context* context, const TextureDescription& desc, const TextureMap& textures)
{
    if (desc.cameras_.Empty() || desc.geometries_.Empty() || !desc.renderPath_)
    {
        // Just fill with color if nothing to render
        const SharedPtr<Image> image = MakeShared<Image>(context);
        image->SetSize(desc.width_, desc.height_, 4);
        image->Clear(desc.color_);

        const SharedPtr<Texture2D> texture = MakeShared<Texture2D>(context);
        texture->SetData(image);
        return texture;
    }
    else
    {
        const Vector<ViewDescription> views = ConstructViewsForTexture(context, desc, textures);
        return RenderViews(context, desc.width_, desc.height_, views);
    }
}

void BuildNormalMapAlpha(SharedPtr<Image> image)
{
    for (int y = 0; y < image->GetHeight(); ++y)
        for (int x = 0; x < image->GetWidth(); ++x)
        {
            const Color pixel = image->GetPixel(x, y);
            image->SetPixel(x, y, Color(pixel, pixel.Luma() > M_LARGE_EPSILON ? 1.0f : 0.0f));
        }
}

SignedDistanceField::SignedDistanceField(const Image& image, bool isTransparent)
    : width_(image.GetWidth())
    , height_(image.GetHeight())
    , data_(width_ * height_)
{
    for (int j = 0; j < height_; ++j)
        for (int i = 0; i < width_; ++i)
        {
            const Color pixel = image.GetPixel(i, j);
            const bool isGap = isTransparent ? pixel.a_ < M_LARGE_EPSILON : pixel.Luma() < M_LARGE_EPSILON;
            if (isGap)
                SetPixel(i, j, Vector3(M_INFINITY, M_INFINITY, M_INFINITY));
            else
                SetPixel(i, j, Vector3::ZERO);
        }

    for (int j = 0; j < height_; ++j)
        for (int i = 0; i < width_; ++i)
        {
            float src = GetPixel(i, j).z_;
            UpdateMinDistance(i, j, -1, -1, src);
            UpdateMinDistance(i, j, 0, -1, src);
            UpdateMinDistance(i, j, 1, -1, src);
            UpdateMinDistance(i, j, -1, 0, src);
        }

    for (int j = height_ - 1; j >= 0; --j)
        for (int i = width_ - 1; i >= 0; --i)
        {
            float src = GetPixel(i, j).z_;
            UpdateMinDistance(i, j, 1, 0, src);
            UpdateMinDistance(i, j, -1, 1, src);
            UpdateMinDistance(i, j, 0, 1, src);
            UpdateMinDistance(i, j, 1, 1, src);
        }
}

const Vector3& SignedDistanceField::GetPixel(int x, int y) const
{
    const IntVector2 xy = Wrap(IntVector2(x, y));
    return data_[xy.y_ * width_ + xy.x_];
}

void SignedDistanceField::SetPixel(int x, int y, const Vector3& value)
{
    const IntVector2 xy = Wrap(IntVector2(x, y));
    data_[xy.y_ * width_ + xy.x_] = value;
}

IntVector2 SignedDistanceField::GetNearestPixel(int x, int y) const
{
    const Vector3 pos = Vector3(static_cast<float>(x), static_cast<float>(y)) + GetPixel(x, y);
    return IntVector2(static_cast<int>(Round(pos.x_)), static_cast<int>(Round(pos.y_)));
}

void SignedDistanceField::UpdateMinDistance(int x, int y, int offsetX, int offsetY, float& currentDist)
{
    Vector3 value = GetPixel(x + offsetX, y + offsetY) + Vector3(static_cast<float>(offsetX), static_cast<float>(offsetY));
    value.z_ = Vector2(value.x_, value.y_).Length();
    if (value.z_ < currentDist)
    {
        currentDist = value.z_;
        SetPixel(x, y, value);
    }
}

IntVector2 SignedDistanceField::Wrap(IntVector2 xy) const
{
    xy.x_ %= width_;
    if (xy.x_ < 0)
        xy.x_ += width_;

    xy.y_ %= height_;
    if (xy.y_ < 0)
        xy.y_ += height_;

    return xy;
}

void FillImageGaps(SharedPtr<Image> image, unsigned downsample)
{
    if (image)
    {
        // Downsample image
        image->CleanupLevels();
        SharedPtr<Image> inputImage = image;
        for (unsigned i = 0; i < downsample; ++i)
            inputImage = inputImage->GetNextLevel();

        // Restore image colors
        if (downsample > 0)
        {
            for (int y = 0; y < image->GetHeight(); ++y)
                for (int x = 0; x < image->GetWidth(); ++x)
                {
                    const Color pixel = inputImage->GetPixel(x, y);
                    if (pixel.a_ > M_LARGE_EPSILON)
                        inputImage->SetPixel(x, y, pixel * (1.0f / pixel.a_));
                }
        }

        const unsigned downsampleFactor = 1 << downsample;
        const SignedDistanceField sdf(*inputImage, true);
        for (int j = 0; j < image->GetHeight(); ++j)
            for (int i = 0; i < image->GetWidth(); ++i)
            {
                const IntVector2 nij = sdf.GetNearestPixel(i / downsampleFactor, j / downsampleFactor);
                const Color fillColor = inputImage->GetPixel(nij.x_, nij.y_);
                const Color pixel = image->GetPixel(i, j);
                image->SetPixel(i, j, pixel.a_ > M_LARGE_EPSILON ? pixel : Color(fillColor, pixel.a_));
            }
    }
}

SharedPtr<Texture2D> GeneratePerlinNoiseOctave(XMLFile* renderPath, Model* model, Material* material,
    unsigned width, unsigned height, const Vector2& scale, float seed)
{
    if (!renderPath || !model || !material)
    {
        URHO3D_LOGERROR("GeneratePerlinNoiseOctave must accept valid render path, model and material");
        return nullptr;
    }
    Context* context = renderPath->GetContext();

    TextureDescription desc;
    desc.renderPath_ = renderPath;
    desc.width_ = Max(1u, width);
    desc.height_ = Max(1u, height);

    GeometryDescription geometryDesc;
    geometryDesc.model_ = model;
    geometryDesc.materials_.Push(SharedPtr<Material>(material));
    desc.geometries_.Push(geometryDesc);

    desc.cameras_.Push(OrthoCameraDescription::Identity(desc.width_, desc.height_));
    desc.parameters_.Populate(inputParameterUniform[0], Vector4(scale.x_, scale.y_, seed, seed));

    return RenderTexture(context, desc, TextureMap());
}

SharedPtr<Image> GeneratePerlinNoise(XMLFile* renderPath, Model* model, Material* material, unsigned width, unsigned height,
    const Color& firstColor, const Color& secondColor, const PODVector<Vector4>& octaves,
    float bias, float contrast, const Vector2& range)
{
    if (!renderPath || !model || !material)
    {
        URHO3D_LOGERROR("GeneratePerlinNoise must accept valid render path, model and material");
        return nullptr;
    }
    Context* context = renderPath->GetContext();

    PODVector<float> buffer(width * height, 0.0f);

    // Apply octaves
    float maxMagnitude = 0.0f;
    for (unsigned i = 0; i < octaves.Size(); ++i)
    {
        // Compute base scale
        const Vector2 textureScale = width > height
            ? Vector2(static_cast<float>(width) / height, 1.0f)
            : Vector2(1.0f, static_cast<float>(height) / width);

        // Read parameters
        const Vector2 scale(octaves[i].x_, octaves[i].y_);
        const float magnitude = octaves[i].z_;
        const float seed = octaves[i].w_;

        // Generate texture
        const SharedPtr<Texture2D> texture = GeneratePerlinNoiseOctave(
            renderPath, model, material, width, height, scale * textureScale, seed);
        const SharedPtr<Image> image = texture ? ConvertTextureToImage(texture) : nullptr;

        // Write to buffer
        if (image)
        {
            assert(image->GetWidth() == width);
            assert(image->GetHeight() == height);
            assert(buffer.Size() == width * height);
            maxMagnitude += magnitude;
            for (unsigned y = 0; y < height; ++y)
            {
                for (unsigned x = 0; x < width; ++x)
                {
                    const float value = image->GetPixel(x, y).r_;
                    buffer[y * width + x] += value * magnitude;
                }
            }
        }
    }

    // Apply modifiers
    for (float& value : buffer)
    {
        // Normalize and apply bias
        value = Clamp(value / maxMagnitude + bias, 0.0f, 1.0f);
        // Apply contrast
        value = SmoothStepEx(value, contrast);
        // Remap to range
        value = Clamp(FloatRange(range).Get(value), 0.0f, 1.0f);
    }

    // Build image
    SharedPtr<Image> image = MakeShared<Image>(context);
    image->SetSize(width, height, 4);
    for (unsigned y = 0; y < height; ++y)
    {
        for (unsigned x = 0; x < width; ++x)
        {
            const float value = buffer[y * width + x];
            const Color color = Lerp(firstColor, secondColor, value);
            image->SetPixel(x, y, color);
        }
    }
    return image;
}

SharedPtr<Image> FillTextureGaps(SharedPtr<Image> image, unsigned depth, bool isTransparent,
    SharedPtr<XMLFile> renderPath, SharedPtr<Model> model, SharedPtr<Material> material, const String& sizeUniform)
{
    // First iteration is input texture, convert image to transparent if needed
    SharedPtr<Texture2D> resultTexture = ConvertImageToTexture(
        isTransparent ? image : ConvertColorKeyToAlpha(image, Color::BLACK));

    // Apply filter
    for (unsigned i = 0; i < depth; ++i)
    {
        TextureDescription desc;
        desc.renderPath_ = renderPath;
        desc.width_ = Max(1, resultTexture->GetWidth());
        desc.height_ = Max(1, resultTexture->GetHeight());

        GeometryDescription geometryDesc;
        geometryDesc.model_ = model;
        geometryDesc.materials_.Push(material);
        desc.geometries_.Push(geometryDesc);

        desc.cameras_.Push(OrthoCameraDescription::Identity(desc.width_, desc.height_));
        desc.textures_.Populate(TU_DIFFUSE, "Input");
        desc.parameters_.Populate(sizeUniform, Vector4(1.0f / desc.width_, 1.0f / desc.height_, 0.0f, 0.0f));

        const TextureMap inputMap = { MakePair(String("Input"), resultTexture) };
        resultTexture = RenderTexture(image->GetContext(), desc, inputMap);
    }

    // Restore original alpha channel
    SharedPtr<Image> resultImage = ConvertTextureToImage(resultTexture);
    if (isTransparent)
    {
        CopyImageAlpha(resultImage, image);
    }
    else
    {
        ResetImageAlpha(resultImage);
    }
    resultImage->SetName(image->GetName());
    return resultImage;
}

void GenerateTexturesFromXML(XMLElement& node, ResourceCache& resourceCache, const FactoryContext& factoryContext)
{
    TextureFactory textureFactory(resourceCache.GetContext());
    textureFactory.SetName(factoryContext.currentDirectory_ + "/[temporary]");
    textureFactory.Load(node);
    if (!textureFactory.CheckAllOutputs(factoryContext.outputDirectory_) || factoryContext.forceGeneration_)
    {
        textureFactory.Generate();
        textureFactory.Save(factoryContext.outputDirectory_);
    }
}

//////////////////////////////////////////////////////////////////////////
TextureFactory::TextureFactory(Context* context)
    : Resource(context)
{
    resourceCache_ = GetSubsystem<ResourceCache>();
    if (!resourceCache_)
    {
        URHO3D_LOGERROR("Resource cache subsystem must be initialized");
        return;
    }
}

void TextureFactory::RegisterObject(Context* context)
{
    context->RegisterFactory<TextureFactory>();
}

bool TextureFactory::BeginLoad(Deserializer& source)
{
    loadXMLFile_ = MakeShared<XMLFile>(context_);
    return loadXMLFile_->Load(source);
}

bool TextureFactory::EndLoad()
{
    if (loadXMLFile_)
    {
        return Load(loadXMLFile_->GetRoot());
    }
    else
    {
        return false;
    }
}

bool TextureFactory::Load(const XMLElement& source)
{
    if (!resourceCache_)
    {
        return false;
    }

    currectDirectory_ = GetFilePath(GetName());

    StringVector texturesNames;
    for (XMLElement textureNode = source.GetChild("texture"); textureNode; textureNode = textureNode.GetNext("texture"))
    {
        TextureDescription textureDesc;

        // Special case for single-color texture
        if (textureNode.HasAttribute("color"))
        {
            const String textureName = textureNode.GetAttribute("name");
            if (textureName.Empty())
            {
                URHO3D_LOGERROR("Texture name must be specified and non-zero");
                return false;
            }

            textureDesc.color_ = textureNode.GetColor("color");
            textureDesc.width_ = 1;
            textureDesc.height_ = 1;
            AddTexture(textureName, textureDesc);
            continue;
        }

        textureDesc.width_ = textureNode.GetUInt("width");
        if (textureDesc.width_ == 0)
        {
            URHO3D_LOGERROR("Texture width must be specified and non-zero");
            return false;
        }
        textureDesc.height_ = textureNode.GetUInt("height");
        if (textureDesc.height_ == 0)
        {
            URHO3D_LOGERROR("Texture height must be specified and non-zero");
            return false;
        }

        // Load geometries
        BoundingBox boundingBox;
        for (XMLElement geometryNode = textureNode.GetChild("geometry"); geometryNode; geometryNode = geometryNode.GetNext("geometry"))
        {
            GeometryDescription geometryDesc;

            // Load model
            if (geometryNode.HasAttribute("model"))
            {
                const String modelName = geometryNode.GetAttribute("model").Trimmed().Replaced("@", currectDirectory_);
                geometryDesc.model_ = resourceCache_->GetResource<Model>(modelName);
                if (!geometryDesc.model_)
                {
                    URHO3D_LOGERRORF("Source geometry model '%s' was not found", modelName.CString());
                    return false;
                }
            }
            else if (geometryNode.HasAttribute("script"))
            {
                const String scriptName = geometryNode.GetAttribute("script").Trimmed().Replaced("@", currectDirectory_);
                const String entryPoint = GetAttribute(geometryNode, "entry", String("Main"));
                SharedPtr<ScriptFile> script(resourceCache_->GetResource<ScriptFile>(scriptName));
                if (!script)
                {
                    URHO3D_LOGERRORF("Source geometry script '%s' was not found", scriptName.CString());
                    return false;
                }
                SharedPtr<ModelFactory> factory = CreateModelFromScript(*script, entryPoint);
                geometryDesc.model_ = factory->BuildModel();
            }

            // Load bounding box
            boundingBox.Merge(geometryDesc.model_->GetBoundingBox());

            // Load materials
            StringVector materialsList = geometryNode.GetAttribute("materials").Trimmed().Replaced("@", currectDirectory_).Split(';');
            for (XMLElement materialNode = geometryNode.GetChild("material"); materialNode; materialNode = materialNode.GetNext("material"))
            {
                materialsList.Push(materialNode.GetAttribute("name").Trimmed().Replaced("@", currectDirectory_));
            }
            for (const String& materialName : materialsList)
            {
                const SharedPtr<Material> material(resourceCache_->GetResource<Material>(materialName));
                geometryDesc.materials_.Push(material);
                if (!material)
                {
                    URHO3D_LOGERRORF("Source geometry material '%s' was not found", materialName.CString());
                    return false;
                }
            }

            textureDesc.geometries_.Push(geometryDesc);
        }

        // Load cameras
        for (XMLElement cameraNode = textureNode.GetChild("camera"); cameraNode; cameraNode = cameraNode.GetNext("camera"))
        {
            textureDesc.cameras_ += GenerateProxyCamerasFromXML(boundingBox, textureDesc.width_, textureDesc.height_, cameraNode);
        }
        if (textureDesc.cameras_.Empty())
        {
            OrthoCameraDescription cameraDesc;

            cameraDesc.position_ = Vector3(0.5f, 0.5f, 0.0f);
            cameraDesc.farClip_ = 1.0f;
            cameraDesc.size_ = Vector2(1.0f, 1.0f);
            cameraDesc.viewport_ = IntRect(0, 0, textureDesc.width_, textureDesc.height_);

            textureDesc.cameras_.Push(cameraDesc);
        }

        // Load textures
        for (XMLElement inputNode = textureNode.GetChild("input"); inputNode; inputNode = inputNode.GetNext("input"))
        {
            const String unitName = inputNode.GetAttribute("unit");
            const TextureUnit unit = ParseTextureUnit(unitName);
            if (unit == MAX_TEXTURE_UNITS)
            {
                URHO3D_LOGERRORF("Unrecognized input texture unit '%s'", unitName.CString());
                return false;
            }

            const String textureName = inputNode.GetAttribute("texture");
            if (textureName.Empty())
            {
                URHO3D_LOGERROR("Input texture name mustn't be empty");
                return false;
            }

            textureDesc.textures_.Populate(unit, textureName);
        }

        // Load textures
        for (XMLElement paramNode = textureNode.GetChild("param"); paramNode; paramNode = paramNode.GetNext("param"))
        {
            const String paramName = paramNode.GetAttribute("name");
            const Variant paramValue = paramNode.GetVectorVariant("value");

            textureDesc.parameters_.Populate(paramName, paramValue);
        }

        // Load all variations
        Vector<Pair<String, String>> variations;
        for (XMLElement variationNode = textureNode.GetChild("variation"); variationNode; variationNode = variationNode.GetNext("variation"))
        {
            variations.Push(MakePair(variationNode.GetAttribute("name"), variationNode.GetAttribute("renderpath")));
        }
        if (!textureNode.HasChild("variation"))
        {
            variations.Push(MakePair(textureNode.GetAttribute("name"), textureNode.GetAttribute("renderpath")));
        }

        for (const Pair<String, String>& variation : variations)
        {
            if (variation.first_.Empty())
            {
                URHO3D_LOGERROR("Texture variation name must be specified and non-empty");
                return false;
            }

            if (FindTexture(variation.first_) >= 0)
            {
                URHO3D_LOGERRORF("Texture variation name '%s' must be unique", variation.first_.CString());
                return false;
            }

            SharedPtr<XMLFile> renderPath(resourceCache_->GetResource<XMLFile>(variation.second_));
            if (!renderPath)
            {
                URHO3D_LOGERRORF("Texture variation render path '%s' was not found", variation.second_.CString());
                return false;
            }

            textureDesc.renderPath_ = renderPath;
            texturesNames.Push(variation.first_);
            AddTexture(variation.first_, textureDesc);
        }
    }

    for (XMLElement outputNode = source.GetChild("output"); outputNode; outputNode = outputNode.GetNext("output"))
    {
        const String& textureName = outputNode.GetAttribute("name");
        if (FindTexture(textureName) < 0)
        {
            URHO3D_LOGERRORF("Output texture '%s' was not found", textureName.CString());
            return false;
        }

        const String& fileName = outputNode.GetAttribute("file").Trimmed().Replaced("@", currectDirectory_);
        AddOutput(textureName, fileName);
    }

    if (outputs_.Empty())
    {
        for (const String& textureName : texturesNames)
        {
            AddOutput(textureName, "");
        }
    }

    return true;
}

bool TextureFactory::AddTexture(const String& name, const TextureDescription& desc)
{
    if (FindTexture(name) >= 0)
    {
        return false;
    }

    textureDescs_.Push(MakePair(name, desc));
    return true;
}

void TextureFactory::RemoveAllTextures()
{
    textureDescs_.Clear();
}

void TextureFactory::AddOutput(const String& name, const String& fileName)
{
    outputs_.Push(MakePair(name, fileName));
}

void TextureFactory::RemoveAllOutputs()
{
    outputs_.Clear();
}

bool TextureFactory::CheckAllOutputs(const String& outputDirectory) const
{
    for (const Pair<String, String>& outputDesc : outputs_)
    {
        if (!resourceCache_->Exists(outputDirectory + outputDesc.second_))
        {
            return false;
        }
    }
    return true;
}

bool TextureFactory::Generate()
{
    if (!resourceCache_)
    {
        return false;
    }

    // Generate textures
    for (const Pair<String, TextureDescription>& textureDesc : textureDescs_)
    {
        SharedPtr<Texture2D> texture = RenderTexture(context_, textureDesc.second_, textureMap_);
        if (!texture)
        {
            URHO3D_LOGERRORF("Cannot generate texture '%s'", textureDesc.first_);
            return false;
        }
        textureMap_.Populate(textureDesc.first_, texture);
    }

    return true;
}

bool TextureFactory::Save(const String& outputDirectory)
{
    // Save textures
    for (const Pair<String, String>& outputDesc : outputs_)
    {
        if (!textureMap_.Contains(outputDesc.first_))
        {
            URHO3D_LOGERRORF("Cannot find procedural texture with internal name '%s'", outputDesc.first_);
            return false;
        }

        const SharedPtr<Texture2D> texture = textureMap_[outputDesc.first_];
        const String outputName = outputDesc.second_;

        // Save
        const String& outputFileName = outputDirectory + outputName;
        CreateDirectoriesToFile(*resourceCache_, outputFileName);
        const SharedPtr<Image> image = ConvertTextureToImage(texture);
        if (image->SavePNG(outputFileName))
        {
            resourceCache_->ReloadResourceWithDependencies(outputName);
        }
        else
        {
            URHO3D_LOGERRORF("Cannot save texture to '%s'", outputFileName.CString());
        }
    }
    return true;
}

Vector<SharedPtr<Texture2D>> TextureFactory::GetTextures() const
{
    Vector<SharedPtr<Texture2D>> textures;
    for (const Pair<String, String>& outputDesc : outputs_)
    {
        const SharedPtr<Texture2D>* texture = textureMap_[outputDesc.first_];
        if (!texture)
        {
            URHO3D_LOGERRORF("Cannot find procedural texture with internal name '%s'", outputDesc.first_);
            textures.Push(nullptr);
        }
        else
        {
            textures.Push(*texture);
        }
    }
    return textures;
}

int TextureFactory::FindTexture(const String& name) const
{
    int idx = 0;
    for (const Pair<String, TextureDescription>& texture : textureDescs_)
    {
        if (name.Compare(texture.first_, false) == 0)
        {
            return idx;
        }
        ++idx;
    }
    return -1;
}

}
