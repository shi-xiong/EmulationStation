#include "TextureResource.h"
#include "../Log.h"
#include "../platform.h"
#include GLHEADER
#include "../ImageIO.h"
#include "../Renderer.h"

std::map< std::string, std::weak_ptr<TextureResource> > TextureResource::sTextureMap;

TextureResource::TextureResource(const std::string& path) : 
	mTextureID(0), mPath(path), mTextureSize(Eigen::Vector2i::Zero())
{
	reload(ResourceManager::getInstance());
}

TextureResource::~TextureResource()
{
	deinit();
}

void TextureResource::unload(std::shared_ptr<ResourceManager>& rm)
{
	deinit();
}

void TextureResource::reload(std::shared_ptr<ResourceManager>& rm)
{
	if(!mPath.empty())
		initFromResource(rm->getFileData(mPath));
}

void TextureResource::initFromResource(const ResourceData data)
{
	//make sure we aren't going to leak an old texture
	deinit();

	size_t width, height;
	std::vector<unsigned char> imageRGBA = ImageIO::loadFromMemoryRGBA32(const_cast<unsigned char*>(data.ptr.get()), data.length, width, height);

	if(imageRGBA.size() == 0)
	{
		LOG(LogError) << "Could not initialize texture (invalid resource data)!";
		return;
	}

	//now for the openGL texture stuff
	glGenTextures(1, &mTextureID);
	glBindTexture(GL_TEXTURE_2D, mTextureID);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, imageRGBA.data());

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	mTextureSize << width, height;
}

void TextureResource::initFromScreen()
{
	deinit();

	int width = Renderer::getScreenWidth();
	int height = Renderer::getScreenHeight();

	glGenTextures(1, &mTextureID);
	glBindTexture(GL_TEXTURE_2D, mTextureID);

	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, width, height, 0);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	mTextureSize[0] = height;
	mTextureSize[1] = height;
}

void TextureResource::initFromMemory(const char* data, size_t length)
{
	deinit();

	size_t width, height;
	std::vector<unsigned char> imageRGBA = ImageIO::loadFromMemoryRGBA32((const unsigned char*)(data), length, width, height);

	if(imageRGBA.size() == 0)
	{
		LOG(LogError) << "Could not initialize texture from memory (invalid data)!";
		return;
	}

	//now for the openGL texture stuff
	glGenTextures(1, &mTextureID);
	glBindTexture(GL_TEXTURE_2D, mTextureID);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, imageRGBA.data());

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	mTextureSize << width, height;
}

void TextureResource::deinit()
{
	if(mTextureID != 0)
	{
		glDeleteTextures(1, &mTextureID);
		mTextureID = 0;
	}
}

Eigen::Vector2i TextureResource::getSize() const
{
	return mTextureSize;
}

void TextureResource::bind() const
{
	if(mTextureID != 0)
		glBindTexture(GL_TEXTURE_2D, mTextureID);
	else
		LOG(LogError) << "Tried to bind uninitialized texture!";
}


std::shared_ptr<TextureResource> TextureResource::get(const std::string& path)
{
	std::shared_ptr<ResourceManager>& rm = ResourceManager::getInstance();

	if(path.empty())
	{
		std::shared_ptr<TextureResource> tex(new TextureResource(""));
		rm->addReloadable(tex); //make sure we're deinitialized even though we do nothing on reinitialization
		return tex;
	}

	auto foundTexture = sTextureMap.find(path);
	if(foundTexture != sTextureMap.end())
	{
		if(!foundTexture->second.expired())
		{
			return foundTexture->second.lock();
		}
	}

	std::shared_ptr<TextureResource> tex = std::shared_ptr<TextureResource>(new TextureResource(path));
	sTextureMap[path] = std::weak_ptr<TextureResource>(tex);
	rm->addReloadable(tex);
	return tex;
}
