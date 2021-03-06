#include "GuiGameList.h"
#include "../InputManager.h"
#include <iostream>
#include "GuiMenu.h"
#include "GuiFastSelect.h"
#include <boost/filesystem.hpp>
#include "../Log.h"
#include "../Settings.h"

#include "GuiMetaDataEd.h"
#include "GuiScraperStart.h"

std::vector<FolderData::SortState> GuiGameList::sortStates;

Eigen::Vector3f GuiGameList::getImagePos()
{
	return Eigen::Vector3f(Renderer::getScreenWidth() * mTheme->getFloat("gameImageOffsetX"), Renderer::getScreenHeight() * mTheme->getFloat("gameImageOffsetY"), 0.0f);
}

namespace {

        // return a list of files that were modified after the given timestamp
        std::vector<std::string> newFilesInDirSince(const std::string &path, const boost::posix_time::ptime &since)
        {
                std::vector<std::string> result;
                if (!path.empty())
                {
                        for (boost::filesystem::directory_iterator it(path), end; it != end; ++it)
                        {
                                std::time_t t = boost::filesystem::last_write_time( *it );
                                const boost::posix_time::ptime lastWriteTime = boost::posix_time::from_time_t( t );
                                if (lastWriteTime >= since)
                                        result.push_back(it->path().generic_string());
                        }
                }
                std::sort(result.begin(), result.end()); // lexical order usually also is chronological order for generated screenshot filenames
                return result;
        }
        // move the given list of files to the destination directory, renaming then to basename-<no>.ext.
        // no existing files will be overwritten!
        std::vector<std::string> moveAndRenameFiles(const std::vector<std::string> &files, const std::string &basename, const std::string &destDir)
        {
                std::vector<std::string> result;
                unsigned int no = 0;
                boost::filesystem::path srcPath, dstPath;
                for (auto fname: files)
                {
                        srcPath = fname;
                        do {
                                dstPath = destDir;
                                dstPath /= basename;
                                dstPath += "-";
                                dstPath += std::to_string(no++);
                                dstPath += srcPath.extension();
                        } while (boost::filesystem::exists(dstPath));
                        LOG(LogDebug) << "moving screenshot " << srcPath.generic_string() << " to " << dstPath.generic_string() << std::endl;
                        boost::filesystem::rename(srcPath, dstPath);
                        result.push_back(dstPath.generic_string());
                }
                return result;
        }
}


bool GuiGameList::isDetailed() const
{
	if(!mFolder)
		return false;

	//return true if any game has an image specified
	for(unsigned int i = 0; i < mFolder->getFileCount(); i++)
	{
		if(!mFolder->getFile(i)->isFolder())
		{
			GameData* game = (GameData*)(mFolder->getFile(i));
			if(game->metadata()->getSize("image") != 0)
				return true;
		}
	}

	return false;
}

GuiGameList::GuiGameList(Window* window) : GuiComponent(window), 
	mTheme(new ThemeComponent(mWindow)),
	mList(window, 0.0f, 0.0f, Font::get(FONT_SIZE_MEDIUM)), 
	mScreenshot(nullptr),
	mScreenshots(nullptr),
	mDescription(window), 
	mRating(window), 
        mLastPlayedLabel(window),
	mLastPlayed(window),
	mReleaseDateLabel(window), 
	mReleaseDate(window), 
	mDescContainer(window), 
	mTransitionImage(window, 0.0f, 0.0f, "", (float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight(), true), 
	mHeaderText(mWindow), 
	sortStateIndex(Settings::getInstance()->getInt("GameListSortIndex")),
	mLockInput(false),
	mEffectFunc(NULL), mEffectTime(0), mGameLaunchEffectLength(700)
{
	//first object initializes the vector
	if (sortStates.empty()) {
		sortStates.push_back(FolderData::SortState(FolderData::compareFileName, true, "file name, ascending"));
		sortStates.push_back(FolderData::SortState(FolderData::compareFileName, false, "file name, descending"));
		sortStates.push_back(FolderData::SortState(FolderData::compareRating, true, "rating, ascending"));
		sortStates.push_back(FolderData::SortState(FolderData::compareRating, false, "rating, descending"));
        sortStates.push_back(FolderData::SortState(FolderData::compareTimesPlayed, true, "played least often"));
        sortStates.push_back(FolderData::SortState(FolderData::compareTimesPlayed, false, "played most often"));
		sortStates.push_back(FolderData::SortState(FolderData::compareLastPlayed, true, "played least recently"));
		sortStates.push_back(FolderData::SortState(FolderData::compareLastPlayed, false, "played most recently"));
	}

        mLastPlayed.setDisplayMode(DateTimeComponent::DISP_RELATIVE_TO_NOW);

	mDescContainer.addChild(&mReleaseDateLabel);
	mDescContainer.addChild(&mReleaseDate);
	mDescContainer.addChild(&mRating);
        mDescContainer.addChild(&mLastPlayedLabel);
	mDescContainer.addChild(&mLastPlayed);
	mDescContainer.addChild(&mDescription);

	//scale delay with screen width (higher width = more text per line)
	//the scroll speed is automatically scaled by component size
	mDescContainer.setAutoScroll((int)(1500 + (Renderer::getScreenWidth() * 0.5)), 0.025f);

	mTransitionImage.setPosition((float)Renderer::getScreenWidth(), 0);
	mTransitionImage.setOrigin(0, 0);

	mHeaderText.setColor(0xFF0000FF);
	mHeaderText.setFont(Font::get(FONT_SIZE_LARGE));
	mHeaderText.setPosition(0, 1);
	mHeaderText.setSize((float)Renderer::getScreenWidth(), 0);
	mHeaderText.setCentered(true);

	addChild(mTheme);
	addChild(&mHeaderText);
	addChild(&mDescContainer);
	addChild(&mList);
	addChild(&mTransitionImage);

	mTransitionAnimation.addChild(this);

    reselectSystem();
}

GuiGameList::~GuiGameList()
{
	delete mTheme;
        if (mScreenshot != nullptr)
        {
                mImageAnimation.removeChild(mScreenshot);
                removeChild(mScreenshot);
                delete mScreenshot;
                mScreenshot = nullptr;
        }
        if (mScreenshots != nullptr)
        {
                mImageAnimation.removeChild(mScreenshots);
                removeChild(mScreenshots);
                delete mScreenshots;
                mScreenshots = nullptr;
        }
}

void GuiGameList::setSystemId(int id)
{
	if(SystemData::sSystemVector.size() == 0)
	{
		LOG(LogError) << "Error - no systems found!";
		return;
	}

	//make sure the id is within range
	if(id >= (int)SystemData::sSystemVector.size())
		id -= SystemData::sSystemVector.size();
	if(id < 0)
		id += SystemData::sSystemVector.size();

	mSystemId = id;
	mSystem = SystemData::sSystemVector.at(mSystemId);

	//clear the folder stack
	while(mFolderStack.size()){ mFolderStack.pop(); }

	mFolder = mSystem->getRootFolder();

        mTheme->setVar("SYSTEM_NAME", mSystem->getName());
        mTheme->setVar("SYSTEM_FULLNAME", mSystem->getFullName());
        mTheme->setVar("SYSTEM_GAMECOUNT", std::to_string(mSystem->getGameCount()));
	updateTheme();
	updateList();
	updateDetailData();
	mWindow->normalizeNextUpdate(); //image loading can be slow
}

void GuiGameList::render(const Eigen::Affine3f& parentTrans)
{
	Eigen::Affine3f trans = parentTrans * getTransform();
	renderChildren(trans);
}

bool GuiGameList::input(InputConfig* config, Input input)
{	
	if(mLockInput)
		return false;

        mList.getSelectedObject()->setSelected(0);
	mList.input(config, input);
        mList.getSelectedObject()->setSelected(true);

	if(input.id == SDLK_F3)
	{
		GameData* game = dynamic_cast<GameData*>(mList.getSelectedObject());
		if(game)
		{
			FolderData* root = mSystem->getRootFolder();
			ScraperSearchParams searchParams;
			searchParams.game = game;
			searchParams.system = mSystem;
			mWindow->pushGui(new GuiMetaDataEd(mWindow, game->metadata(), mSystem->getGameMDD(), searchParams, game->getBaseName(),
				[&] { updateDetailData(); }, 
				[game, root, this] { 
					boost::filesystem::remove(game->getPath());
					root->removeFileRecursive(game); 
					updateList(); 
			}));
		}
		return true;
	}

	if(input.id == SDLK_F5)
	{
		mWindow->pushGui(new GuiScraperStart(mWindow));
		return true;
	}

	if(config->isMappedTo("a", input) && mFolder->getFileCount() > 0 && input.value != 0)
	{
		//play select sound
		mTheme->getSound("menuSelect")->play();

		FileData* file = mList.getSelectedObject();
		if(file->isFolder()) //if you selected a folder, add this directory to the stack, and use the selected one
		{
			mFolderStack.push(mFolder);
			mFolder = (FolderData*)file;
			updateList();
			updateDetailData();
			return true;
		}else{
			mList.stopScrolling();

			mEffectFunc = &GuiGameList::updateGameLaunchEffect;
			mEffectTime = 0;
			mGameLaunchEffectLength = (int)mTheme->getSound("menuSelect")->getLengthMS();
			if(mGameLaunchEffectLength < 800)
				mGameLaunchEffectLength = 800;

			mLockInput = true;

			return true;
		}
	}

	//if there's something on the directory stack, return to it
	if(config->isMappedTo("b", input) && input.value != 0 && mFolderStack.size())
	{
		mFolder = mFolderStack.top();
		mFolderStack.pop();
		updateList();
		updateDetailData();

		//play the back sound
		mTheme->getSound("menuBack")->play();

		return true;
	}

	//only allow switching systems if more than one exists (otherwise it'll reset your position when you switch and it's annoying)
	if(SystemData::sSystemVector.size() > 1 && input.value != 0)
	{
		if(config->isMappedTo("right", input))
		{
			setSystemId(mSystemId + 1);
			doTransition(-1);
			return true;
		}
		if(config->isMappedTo("left", input))
		{
			setSystemId(mSystemId - 1);
			doTransition(1);
			return true;
		}
	}

	//change sort order
	if(config->isMappedTo("sortordernext", input) && input.value != 0) {
		setNextSortIndex();
		//std::cout << "Sort order is " << FolderData::getSortStateName(sortStates.at(sortStateIndex).comparisonFunction, sortStates.at(sortStateIndex).ascending) << std::endl;
	}
	else if(config->isMappedTo("sortorderprevious", input) && input.value != 0) {
		setPreviousSortIndex();
		//std::cout << "Sort order is " << FolderData::getSortStateName(sortStates.at(sortStateIndex).comparisonFunction, sortStates.at(sortStateIndex).ascending) << std::endl;
	}

	//open the "start menu"
	if(config->isMappedTo("menu", input) && input.value != 0)
	{
		mWindow->pushGui(new GuiMenu(mWindow, this));
		return true;
	}

	//open the fast select menu
	if(config->isMappedTo("select", input) && input.value != 0)
	{
        mWindow->pushGui(new GuiFastSelect(mWindow, this, &mList, mList.getSelectedObject()->getName()[0], mTheme));
		return true;
	}

	if(isDetailed())
	{
		if(config->isMappedTo("up", input) || config->isMappedTo("down", input) || config->isMappedTo("pageup", input) || config->isMappedTo("pagedown", input))
		{
			if(input.value == 0)
				updateDetailData();
			else
				hideDetailData();
		}
		return true;
	}

	return false;
}

const FolderData::SortState & GuiGameList::getSortState() const
{
    return sortStates.at(sortStateIndex);
}

void GuiGameList::setSortIndex(size_t index)
{
	//make the index valid
	if (index >= sortStates.size()) {
		index = 0;
	}
	if (index != sortStateIndex) {
		//get sort state from vector and sort list
		sortStateIndex = index;
		sort(sortStates.at(sortStateIndex).comparisonFunction, sortStates.at(sortStateIndex).ascending);
	}
    //save new index to settings
    Settings::getInstance()->setInt("GameListSortIndex", sortStateIndex);
}

void GuiGameList::setNextSortIndex()
{
	//make the index wrap around
	if ((sortStateIndex - 1) >= sortStates.size()) {
		setSortIndex(0);
	}
	setSortIndex(sortStateIndex + 1);
}

void GuiGameList::setPreviousSortIndex()
{
	//make the index wrap around
	if (((int)sortStateIndex - 1) < 0) {
		setSortIndex(sortStates.size() - 1);
	}
	setSortIndex(sortStateIndex - 1);
}

void GuiGameList::sort(FolderData::ComparisonFunction & comparisonFunction, bool ascending)
{
	//resort list and update it
	mFolder->sort(comparisonFunction, ascending);
	updateList();
	updateDetailData();
}

void GuiGameList::reselectSystem()
{
        boost::posix_time::ptime lastSelectionTime = boost::date_time::min_date_time;
        int lastSelectedSystemId = 0;
        for (unsigned int systemId = 0; systemId < SystemData::sSystemVector.size(); ++systemId)
        {
                SystemData *sd = SystemData::sSystemVector.at(systemId);
                if (sd->getRootFolder()->isSelected() != boost::date_time::not_a_date_time)
                        if (lastSelectionTime < sd->getRootFolder()->isSelected())
                        {
                                lastSelectionTime = sd->getRootFolder()->isSelected();
                                lastSelectedSystemId = systemId;
                        }
        }
        setSystemId(lastSelectedSystemId);
}

void GuiGameList::updateList()
{
	mList.clear();

        unsigned int selectId = 0;
        boost::posix_time::ptime selectIdSelectionTime(boost::date_time::min_date_time);
	for(unsigned int i = 0; i < mFolder->getFileCount(); i++)
	{
		FileData* file = mFolder->getFile(i);
                if (file->isSelected() != boost::date_time::not_a_date_time
                               && file->isSelected() > selectIdSelectionTime)
                {
                        selectId = i;
                        selectIdSelectionTime = file->isSelected();
                }
		if(file->isFolder())
			mList.addObject(file->getName(), file, mTheme->getColor("secondary"));
		else
			mList.addObject(file->getName(), file, mTheme->getColor("primary"));
	}
        mList.setSelection(selectId);
}

std::string GuiGameList::getThemeFile()
{
	std::string themePath;

	themePath = getHomePath();
	themePath += "/.emulationstation/" +  mSystem->getName() + "/theme.xml";
	if(boost::filesystem::exists(themePath))
		return themePath;

	themePath = mSystem->getStartPath() + "/theme.xml";
	if(boost::filesystem::exists(themePath))
		return themePath;

	themePath = getHomePath();
	themePath += "/.emulationstation/es_theme.xml";
	if(boost::filesystem::exists(themePath))
		return themePath;

	return "";
}

void GuiGameList::updateTheme()
{
	mTheme->readXML(getThemeFile(), isDetailed());

	mList.setSelectorColor(mTheme->getColor("selector"));
	mList.setSelectedTextColor(mTheme->getColor("selected"));
	mList.setScrollSound(mTheme->getSound("menuScroll"));

	mList.setFont(mTheme->getListFont());
	mList.setPosition(0.0f, Font::get(FONT_SIZE_LARGE)->getHeight() + 2.0f);

	if(!mTheme->getBool("hideHeader"))
	{
		mHeaderText.setText(mSystem->getFullName());
	}else{
		mHeaderText.setText("");
	}

	if(isDetailed())
	{
		mList.setCentered(mTheme->getBool("listCentered"));

		mList.setPosition(mTheme->getFloat("listOffsetX") * Renderer::getScreenWidth(), mList.getPosition().y());
		mList.setTextOffsetX((int)(mTheme->getFloat("listTextOffsetX") * Renderer::getScreenWidth()));

                if (mTheme->getBool("gameImagesMulti"))
                {
                        if (mScreenshots == nullptr)
                        {
                                if (mScreenshot != nullptr)
                                {
                                        mImageAnimation.removeChild(mScreenshot);
                                        removeChild(mScreenshot);
                                        delete mScreenshot;
                                        mScreenshot = nullptr;
                                }
                                mScreenshots = new VerticalImageAutoScrollbox(mWindow);
                                mImageAnimation.addChild(mScreenshots);
                                mScreenshots->setAutoScroll(1500, 500);
                                addChild(mScreenshots);
                        }
                        mScreenshots->setPosition(
                                        mTheme->getFloat("gameImageOffsetX") * Renderer::getScreenWidth(),
                                        mTheme->getFloat("gameImageOffsetY") * Renderer::getScreenHeight());
                        mScreenshots->setSize(
                                        mTheme->getFloat("gameImageWidth") * Renderer::getScreenWidth(),
                                        mTheme->getFloat("gameImageHeight") * Renderer::getScreenHeight());
                        mScreenshots->setAllowImageUpscale(mTheme->getBool("gameImagesUpscale"));
                        mScreenshots->setBorderSpace(mTheme->getFloat("gameImageSpace") * Renderer::getScreenHeight());
                } else {
                        if (mScreenshot == nullptr)
                        {
                                if (mScreenshots != nullptr)
                                {
                                        mImageAnimation.removeChild(mScreenshots);
                                        removeChild(mScreenshots);
                                        delete mScreenshots;
                                        mScreenshots = nullptr;
                                }
                                mScreenshot = new ImageComponent(mWindow);
                                mImageAnimation.addChild(mScreenshot);
                                addChild(mScreenshot);
                        }
                        mScreenshot->setPosition(
                                        mTheme->getFloat("gameImageOffsetX") * Renderer::getScreenWidth(),
                                        mTheme->getFloat("gameImageOffsetY") * Renderer::getScreenHeight());
                        mScreenshot->setOrigin(
                                        mTheme->getFloat("gameImageOriginX"),
                                        mTheme->getFloat("gameImageOriginY"));
                        mScreenshot->setResize(
                                        mTheme->getFloat("gameImageWidth") * Renderer::getScreenWidth(),
                                        mTheme->getFloat("gameImageHeight") * Renderer::getScreenHeight(),
                                        false);
                }


		mLastPlayedLabel.setColor(mTheme->getColor("description"));
		mLastPlayedLabel.setFont(mTheme->getDescriptionFont());
		mLastPlayed.setColor(mTheme->getColor("description"));
		mLastPlayed.setFont(mTheme->getDescriptionFont());
		mReleaseDateLabel.setColor(mTheme->getColor("description"));
		mReleaseDateLabel.setFont(mTheme->getDescriptionFont());
		mReleaseDate.setColor(mTheme->getColor("description"));
		mReleaseDate.setFont(mTheme->getDescriptionFont());

		mDescription.setColor(mTheme->getColor("description"));
		mDescription.setFont(mTheme->getDescriptionFont());
	}else{
		mList.setCentered(true);
		mList.setPosition(0, mList.getPosition().y());
		mList.setTextOffsetX(0);
	}
}

void GuiGameList::updateDetailData()
{
	if(!isDetailed() || !mList.getSelectedObject() || mList.getSelectedObject()->isFolder())
	{
		hideDetailData();
	}else{
		if(mDescContainer.getParent() != this)
			addChild(&mDescContainer);
                if(mScreenshot != nullptr && mScreenshot->getParent() != this)
			addChild(mScreenshot);
                else if(mScreenshots != nullptr && mScreenshots->getParent() != this)
			addChild(mScreenshots);

		GameData* game = (GameData*)mList.getSelectedObject();

                float gameImageYOffset = 0.f;
                Eigen::Vector3f imgOffset = Eigen::Vector3f(Renderer::getScreenWidth() * 0.10f, 0, 0);
                if (mScreenshot != nullptr)
                {
                        //set image to either "not found" image or metadata image
                        if(game->metadata()->getSize("image") == 0 || !boost::filesystem::exists(game->metadata()->getElemAt("image", 0)))
                        {
                                //image doesn't exist
                                if(mTheme->getString("imageNotFoundPath").empty())
                                {
                                        //"not found" image doesn't exist
                                        mScreenshot->setImage("");
                                        mScreenshot->setSize(0, 0); //clear old size
                                }else{
                                        mScreenshot->setImage(mTheme->getString("imageNotFoundPath"));
                                }
                        }else{
                                mScreenshot->setImage(game->metadata()->getElemAt("image", 0));
                        }

                        mScreenshot->setPosition(getImagePos() - imgOffset);
                        gameImageYOffset = getImagePos().y() + mScreenshot->getSize().y();
                } else if (mScreenshots != nullptr) {
                        // remove old images
                        while (mScreenshots->getChildCount() > 0)
                        {
                                GuiComponent *p = mScreenshots->getChild(0);
                                mScreenshots->removeChild(p);
                                delete p;
                        }
                        //set image to either "not found" image or metadata image
                        if(game->metadata()->getSize("image") == 0)
                        {
                                if (!mTheme->getString("imageNotFoundPath").empty())
                                {
                                        ImageComponent *ic = new ImageComponent(mWindow);
                                        ic->setImage(mTheme->getString("imageNotFoundPath"));
                                        mScreenshots->addImage(ic);
                                }
                        } else {
                                for (unsigned int i=0; i<game->metadata()->getSize("image"); ++i)
                                {
                                        ImageComponent *ic = new ImageComponent(mWindow);
                                        ic->setImage(game->metadata()->getElemAt("image", i));
                                        mScreenshots->addImage(ic);
                                }
                        }

                        mScreenshots->setPosition(getImagePos() - imgOffset); 
                        mScreenshots->reset();
                        gameImageYOffset = getImagePos().y() + mScreenshots->getSize().y();
                }

		mImageAnimation.fadeIn(35);
		mImageAnimation.move((int)imgOffset.x(), (int)imgOffset.y(), 20);

		mDescContainer.setPosition(Eigen::Vector3f(Renderer::getScreenWidth() * 0.03f, gameImageYOffset + 12, 0));
		mDescContainer.setSize(Eigen::Vector2f(Renderer::getScreenWidth() * (mTheme->getFloat("listOffsetX") - 0.03f), Renderer::getScreenHeight() - mDescContainer.getPosition().y()));
		mDescContainer.setScrollPos(Eigen::Vector2d(0, 0));
		mDescContainer.resetAutoScrollTimer();

		const float colwidth = mDescContainer.getSize().x();
		float ratingHeight = colwidth * 0.3f / 5.0f;
		mRating.setSize(ratingHeight * 5.0f, ratingHeight);

		mReleaseDateLabel.setPosition(0, 0);
		mReleaseDateLabel.setText("Released: ");
		mReleaseDate.setPosition(mReleaseDateLabel.getPosition().x() + mReleaseDateLabel.getSize().x(), mReleaseDateLabel.getPosition().y());
		mReleaseDate.setValue(game->metadata()->get("releasedate"));

		mRating.setPosition(colwidth - mRating.getSize().x() - 12, 0);
		mRating.setValue(game->metadata()->get("rating"));

                mLastPlayedLabel.setPosition(0, mReleaseDateLabel.getPosition().y() + mReleaseDateLabel.getSize().y());
                mLastPlayedLabel.setText("Last played: ");
                mLastPlayed.setSize(colwidth - mRating.getSize().x(), ratingHeight);
                mLastPlayed.setPosition(mLastPlayedLabel.getPosition().x() + mLastPlayedLabel.getSize().x(), mLastPlayedLabel.getPosition().y());
                mLastPlayed.setValue(game->metadata()->get("lastplayed"));

		mDescription.setPosition(0, mRating.getSize().y());
		mDescription.setSize(Eigen::Vector2f(Renderer::getScreenWidth() * (mTheme->getFloat("listOffsetX") - 0.03f), 0));
		mDescription.setText(game->metadata()->get("desc"));
	}
}

void GuiGameList::hideDetailData()
{
	if(mDescContainer.getParent() == this)
		removeChild(&mDescContainer);

	mImageAnimation.fadeOut(35);
}

GuiGameList* GuiGameList::create(Window* window)
{
	GuiGameList* list = new GuiGameList(window);
	window->pushGui(list);
	return list;
}

void GuiGameList::update(int deltaTime)
{
	mTransitionAnimation.update(deltaTime);
	mImageAnimation.update(deltaTime);

	if(mEffectFunc != NULL)
	{
		mEffectTime += deltaTime;
		(this->*mEffectFunc)(mEffectTime);
	}

	GuiComponent::update(deltaTime);
}

void GuiGameList::doTransition(int dir)
{
	mTransitionImage.copyScreen();
	mTransitionImage.setOpacity(255);

	//put the image of what's currently onscreen at what will be (in screen coords) 0, 0
	mTransitionImage.setPosition((float)Renderer::getScreenWidth() * dir, 0);

	//move the entire thing offscreen so we'll move into place
	setPosition((float)Renderer::getScreenWidth() * -dir, mPosition[1]);

	mTransitionAnimation.move(Renderer::getScreenWidth() * dir, 0, 50);
}

float lerpFloat(const float& start, const float& end, float t)
{
	if(t <= 0)
		return start;
	if(t >= 1)
		return end;

	return (start * (1 - t) + end * t);
}

Eigen::Vector2f lerpVector2f(const Eigen::Vector2f& start, const Eigen::Vector2f& end, float t)
{
	if(t <= 0)
		return start;
	if(t >= 1)
		return end;

	return (start * (1 - t) + end * t);
}

float clamp(float min, float max, float val)
{
	if(val < min)
		val = min;
	else if(val > max)
		val = max;

	return val;
}

//http://en.wikipedia.org/wiki/Smoothstep
float smoothStep(float edge0, float edge1, float x)
{
    // Scale, and clamp x to 0..1 range
    x = clamp(0, 1, (x - edge0)/(edge1 - edge0));
	
    // Evaluate polynomial
    return x*x*x*(x*(x*6 - 15) + 10);
}

void GuiGameList::updateGameLaunchEffect(int t)
{
	const int endTime = mGameLaunchEffectLength;

	const int zoomTime = endTime;
	const int centerTime = endTime - 50;

	const int fadeDelay = endTime - 600;
	const int fadeTime = endTime - fadeDelay - 100;

        Eigen::Vector2f imageCenter;
        if (mScreenshot != nullptr)
        {
                imageCenter = mScreenshot->getCenter();
        } else if (mScreenshots != nullptr) {
                imageCenter[0] = (mScreenshots->getSize().x()/2 + mScreenshots->getPosition().x());
                imageCenter[1] = (mScreenshots->getSize().y()/2 + mScreenshots->getPosition().y());
        }
	if(!isDetailed())
	{
		imageCenter << mList.getPosition().x() + mList.getSize().x() / 2, mList.getPosition().y() + mList.getSize().y() / 2;
	}

	const Eigen::Vector2f centerStart(Renderer::getScreenWidth() / 2, Renderer::getScreenHeight() / 2);

	//remember to clamp or zoom factor will be incorrect with a negative t because squared
	const float tNormalized = clamp(0, 1, (float)t / endTime);

	mWindow->setCenterPoint(lerpVector2f(centerStart, imageCenter, smoothStep(0.0, 1.0, tNormalized)));
	mWindow->setZoomFactor(lerpFloat(1.0f, 3.0f, tNormalized*tNormalized));
	mWindow->setFadePercent(lerpFloat(0.0f, 1.0f, (float)(t - fadeDelay) / fadeTime));

	if(t > endTime)
	{
		//effect done
		mTransitionImage.setImage(""); //fixes "tried to bind uninitialized texture!" since copyScreen()'d textures don't reinit
                boost::posix_time::ptime time = boost::posix_time::second_clock::universal_time();
		mSystem->launchGame(mWindow, (GameData*)mList.getSelectedObject());
                importFreshScreenshots(time);
                updateDetailData(); // update metadata that may be used in theme (e.g. last played timestamp, new screenshots etc.)
		mEffectFunc = &GuiGameList::updateGameReturnEffect;
		mEffectTime = 0;
		mGameLaunchEffectLength = 700;
		mLockInput = false;
	}
}

void GuiGameList::updateGameReturnEffect(int t)
{
	updateGameLaunchEffect(mGameLaunchEffectLength - t);

	if(t >= mGameLaunchEffectLength)
		mEffectFunc = NULL;
}

void GuiGameList::importFreshScreenshots(const boost::posix_time::ptime &since)
{
        if (mSystem->getEmulatorScreenshotDumpDir().empty() || mSystem->getScreenshotDir().empty())
                return; // not configured

        std::vector<std::string> newScreenshots(newFilesInDirSince(mSystem->getEmulatorScreenshotDumpDir(), since));
        if (newScreenshots.empty())
                return; // no new screenshots found

        GameData* game = dynamic_cast<GameData*>(mList.getSelectedObject());
        if (game == nullptr)
                return; // impossible

	LOG(LogInfo) << "Found " << newScreenshots.size() << " new screenshots for game " << game->getName() << std::endl;
        newScreenshots = moveAndRenameFiles(newScreenshots, game->getBaseName(), mSystem->getScreenshotDir());
        for (auto fname: newScreenshots)
                game->metadata()->push_back("image", fname);
}
