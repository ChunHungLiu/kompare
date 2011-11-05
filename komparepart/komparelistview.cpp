/***************************************************************************
                                komparelistview.h
                                -----------------
        begin                   : Sun Mar 4 2001
        Copyright 2001-2009 Otto Bruggeman <bruggie@gmail.com>
        Copyright 2001-2003 John Firebaugh <jfirebaugh@kde.org>
        Copyright 2004      Jeff Snyder    <jeff@caffeinated.me.uk>
        Copyright 2007-2011 Kevin Kofler   <kevin.kofler@chello.at>
****************************************************************************/

/***************************************************************************
**
**   This program is free software; you can redistribute it and/or modify
**   it under the terms of the GNU General Public License as published by
**   the Free Software Foundation; either version 2 of the License, or
**   (at your option) any later version.
**
***************************************************************************/

#include "komparelistview.h"

#include <QtGui/QPainter>
#include <QtCore/QRegExp>
#include <QtCore/QTimer>
#include <QtGui/QResizeEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QScrollBar>

#include <kdebug.h>
#include <kglobal.h>
#include <kglobalsettings.h>

#include "diffmodel.h"
#include "diffhunk.h"
#include "difference.h"
#include "viewsettings.h"
#include "komparemodellist.h"
#include "komparesplitter.h"

#define COL_LINE_NO      0
#define COL_MAIN         1

#define BLANK_LINE_HEIGHT 3
#define HUNK_LINE_HEIGHT  5

#define ITEM_MARGIN 3

using namespace Diff2;

KompareListViewFrame::KompareListViewFrame( bool isSource,
                                            ViewSettings* settings,
                                            KompareSplitter* parent,
                                            const char* name ):
	QFrame ( parent ),
	m_view ( isSource, settings, this, name ),
	m_label ( isSource?"Source":"Dest", this ),
	m_layout ( this )
{
	setSizePolicy ( QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored) );
	m_label.setSizePolicy ( QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed) );
	QFrame *bottomLine = new QFrame(this);
	bottomLine->setFrameShape(QFrame::HLine);
	bottomLine->setFrameShadow ( QFrame::Plain );
	bottomLine->setSizePolicy ( QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed) );
	bottomLine->setFixedHeight(1);
	m_label.setMargin(3);
	m_layout.setSpacing(0);
	m_layout.setMargin(0);
	m_layout.addWidget(&m_label);
	m_layout.addWidget(bottomLine);
	m_layout.addWidget(&m_view);

	connect( &m_view, SIGNAL(differenceClicked(const Diff2::Difference*)),
	         parent, SLOT(slotDifferenceClicked(const Diff2::Difference*)) );

	connect( parent, SIGNAL(scrollViewsToId(int)), &m_view, SLOT(scrollToId(int)) );
	connect( parent, SIGNAL(setXOffset(int)), &m_view, SLOT(setXOffset(int)) );
	connect( &m_view, SIGNAL(resized()), parent, SLOT(slotUpdateScrollBars()) );
}

void KompareListViewFrame::slotSetModel( const DiffModel* model )
{
	if( model )
	{
		if( view()->isSource() ) {
			if( !model->sourceRevision().isEmpty() )
				m_label.setText( model->sourceFile() + " (" + model->sourceRevision() + ')' );
			else
				m_label.setText( model->sourceFile() );
		} else {
			if( !model->destinationRevision().isEmpty() )
				m_label.setText( model->destinationFile() + " (" + model->destinationRevision() + ')' );
			else
				m_label.setText( model->destinationFile() );
		}
	} else {
		m_label.setText( QString::null );	//krazy:exclude=nullstrassign for old broken gcc
	}
}

KompareListView::KompareListView( bool isSource,
                                  ViewSettings* settings,
                                  QWidget* parent, const char* name ) :
	QTreeWidget( parent ),
	m_isSource( isSource ),
	m_settings( settings ),
	m_scrollId( -1 ),
	m_selectedModel( 0 ),
	m_selectedDifference( 0 )
{
	setObjectName( name );
	setItemDelegate( new KompareListViewItemDelegate( this ) );
	setHeaderHidden( true );
	setColumnCount( 3 ); // Line Number, Main, Blank
	setAllColumnsShowFocus( true );
	setRootIsDecorated( false );
	setIndentation( 0 );
	setFrameStyle( QFrame::NoFrame );
	setVerticalScrollMode( QAbstractItemView::ScrollPerPixel );
	setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
	setFocusPolicy( Qt::NoFocus );
	setFont( m_settings->m_font );
	setFocusProxy( parent->parentWidget() );
}

KompareListView::~KompareListView()
{
	m_settings = 0;
	m_selectedModel = 0;
	m_selectedDifference = 0;
}

KompareListViewItem* KompareListView::itemAtIndex( int i )
{
	return m_items[ i ];
}

int KompareListView::firstVisibleDifference()
{
	QTreeWidgetItem* item = itemAt( QPoint( 0, 0 ) );

	if( item == 0 )
	{
		kDebug(8104) << "no item at viewport coordinates (0,0)" << endl;
	}

	while( item ) {
		KompareListViewLineItem* lineItem = dynamic_cast<KompareListViewLineItem*>(item);
		if( lineItem && lineItem->diffItemParent()->difference()->type() != Difference::Unchanged )
			break;
		item = itemBelow(item);
	}

	if( item )
		return m_items.indexOf( ((KompareListViewLineItem*)item)->diffItemParent() );

	return -1;
}

int KompareListView::lastVisibleDifference()
{
	QTreeWidgetItem* item = itemAt( QPoint( 0, visibleHeight() - 1 ) );

	if( item == 0 )
	{
		kDebug(8104) << "no item at viewport coordinates (0," << visibleHeight() - 1 << ")" << endl;
		// find last item
		item = itemAt( QPoint( 0, 0 ) );
		if( item ) {
			QTreeWidgetItem* nextItem = item;
			do {
				item = nextItem;
				nextItem = itemBelow( item );
			} while( nextItem );
		}
	}

	while( item ) {
		KompareListViewLineItem* lineItem = dynamic_cast<KompareListViewLineItem*>(item);
		if( lineItem && lineItem->diffItemParent()->difference()->type() != Difference::Unchanged )
			break;
		item = itemAbove(item);
	}

	if( item )
		return m_items.indexOf( ((KompareListViewLineItem*)item)->diffItemParent() );

	return -1;
}

QRect KompareListView::totalVisualItemRect( QTreeWidgetItem* item )
{
	QRect total = visualItemRect( item );
	int n = item->childCount();
	for( int i=0; i<n; i++ ) {
		QTreeWidgetItem* child = item->child( i );
		if( !child->isHidden() )
			total = total.united( totalVisualItemRect( child ) );
	}
	return total;
}

QRect KompareListView::itemRect( int i )
{
	QTreeWidgetItem* item = itemAtIndex( i );
	return totalVisualItemRect( item );
}

int KompareListView::minScrollId()
{
	return visibleHeight() / 2;
}

int KompareListView::maxScrollId()
{
	int n = topLevelItemCount();
	if(!n) return 0;
	KompareListViewItem* item = (KompareListViewItem*)topLevelItem( n-1 );
	int maxId = item->scrollId() + item->maxHeight() - minScrollId();
	kDebug(8104) << "Max ID = " << maxId << endl;
	return maxId;
}

int KompareListView::contentsHeight()
{
	return verticalScrollBar()->maximum() + viewport()->height();
}

int KompareListView::contentsWidth()
{
	return ( columnWidth(COL_LINE_NO) + columnWidth(COL_MAIN) );
}

int KompareListView::visibleHeight()
{
	return viewport()->height();
}

int KompareListView::visibleWidth()
{
	return viewport()->width();
}

int KompareListView::contentsX()
{
	return horizontalOffset();
}

int KompareListView::contentsY()
{
	return verticalOffset();
}

void KompareListView::setXOffset( int x )
{
	kDebug(8104) << "SetXOffset : Scroll to x position: " << x << endl;
	horizontalScrollBar()->setValue( x );
}

void KompareListView::scrollToId( int id )
{
//	kDebug(8104) << "ScrollToID : Scroll to id : " << id << endl;
	int n = topLevelItemCount();
	KompareListViewItem* item = 0;
	if( n ) {
		int i = 1;
		for( ; i<n; i++ ) {
			if( ((KompareListViewItem*)topLevelItem( i ))->scrollId() > id )
				break;
		}
		item = (KompareListViewItem*)topLevelItem( i-1 );
	}

	if( item ) {
		QRect rect = totalVisualItemRect( item );
		int pos = rect.top() + verticalOffset();
		int itemId = item->scrollId();
		int height = rect.height();
		double r = (double)( id - itemId ) / (double)item->maxHeight();
		int y = pos + (int)( r * (double)height ) - minScrollId();
//		kDebug(8104) << "scrollToID: " << endl;
//		kDebug(8104) << "            id = " << id << endl;
//		kDebug(8104) << "           pos = " << pos << endl;
//		kDebug(8104) << "        itemId = " << itemId << endl;
//		kDebug(8104) << "             r = " << r << endl;
//		kDebug(8104) << "        height = " << height << endl;
//		kDebug(8104) << "         minID = " << minScrollId() << endl;
//		kDebug(8104) << "             y = " << y << endl;
//		kDebug(8104) << "contentsHeight = " << contentsHeight() << endl;
//		kDebug(8104) << "         c - y = " << contentsHeight() - y << endl;
		verticalScrollBar()->setValue( y );
	}

	m_scrollId = id;
}

int KompareListView::scrollId()
{
	if( m_scrollId < 0 )
		m_scrollId = minScrollId();
	return m_scrollId;
}

void KompareListView::setSelectedDifference( const Difference* diff, bool scroll )
{
	kDebug(8104) << "KompareListView::setSelectedDifference(" << diff << ", " << scroll << ")" << endl;

	// When something other than a click causes this function to be called,
	// it'll only get called once, and all is simple.
	//
	// When the user clicks on a diff, this function will get called once when
	// komparesplitter::slotDifferenceClicked runs, and again when the
	// setSelection signal from the modelcontroller arrives.
	//
	// the first call (which will always be from the splitter) will have
	// scroll==false, and the second call will bail out here.
	// Which is why clicking on a difference does not cause the listviews to
	// scroll.
	if ( m_selectedDifference == diff )
		return;

	m_selectedDifference = diff;

	KompareListViewItem* item = m_itemDict[ diff ];
	if( !item ) {
		kDebug(8104) << "KompareListView::slotSetSelection(): couldn't find our selection!" << endl;
		return;
	}

	// why does this not happen when the user clicks on a diff? see the comment above.
	if( scroll )
		scrollToId(item->scrollId());
	setUpdatesEnabled( false );
	int x = horizontalScrollBar()->value();
	int y = verticalScrollBar()->value();
	setCurrentItem( item );
	horizontalScrollBar()->setValue( x );
	verticalScrollBar()->setValue( y );
	setUpdatesEnabled( true );
}

void KompareListView::slotSetSelection( const Difference* diff )
{
	kDebug(8104) << "KompareListView::slotSetSelection( const Difference* diff )" << endl;

	setSelectedDifference( diff, true );
}

void KompareListView::slotSetSelection( const DiffModel* model, const Difference* diff )
{
	kDebug(8104) << "KompareListView::slotSetSelection( const DiffModel* model, const Difference* diff )" << endl;

	if( m_selectedModel && m_selectedModel == model ) {
		slotSetSelection( diff );
		return;
	}

	clear();
	m_items.clear();
	m_itemDict.clear();
	m_selectedModel = model;

	DiffHunkListConstIterator hunkIt = model->hunks()->begin();
	DiffHunkListConstIterator hEnd   = model->hunks()->end();

	KompareListViewItem* item = 0;

	for ( ; hunkIt != hEnd; ++hunkIt )
	{
		if( item )
			item = new KompareListViewHunkItem( this, item, *hunkIt, model->isBlended() );
		else
			item = new KompareListViewHunkItem( this, *hunkIt, model->isBlended() );

		DifferenceListConstIterator diffIt = (*hunkIt)->differences().begin();
		DifferenceListConstIterator dEnd   = (*hunkIt)->differences().end();

		for ( ; diffIt != dEnd; ++diffIt )
		{
			item = new KompareListViewDiffItem( this, item, *diffIt );

			int type = (*diffIt)->type();

			if ( type != Difference::Unchanged )
			{
				m_items.append( (KompareListViewDiffItem*)item );
				m_itemDict.insert( *diffIt, (KompareListViewDiffItem*)item );
			}
		}
	}

	resizeColumnToContents( COL_LINE_NO );
	resizeColumnToContents( COL_MAIN );

	slotSetSelection( diff );
}

void KompareListView::mousePressEvent( QMouseEvent* e )
{
	QPoint vp = e->pos();
	KompareListViewLineItem* lineItem = dynamic_cast<KompareListViewLineItem*>( itemAt( vp ) );
	if( !lineItem )
		return;
	KompareListViewDiffItem* diffItem = lineItem->diffItemParent();
	if( diffItem->difference()->type() != Difference::Unchanged ) {
		emit differenceClicked( diffItem->difference() );
	}
}

void KompareListView::mouseDoubleClickEvent( QMouseEvent* e )
{
	QPoint vp = e->pos();
	KompareListViewLineItem* lineItem = dynamic_cast<KompareListViewLineItem*>( itemAt( vp ) );
	if ( !lineItem )
		return;
	KompareListViewDiffItem* diffItem = lineItem->diffItemParent();
	if ( diffItem->difference()->type() != Difference::Unchanged ) {
		// FIXME: make a new signal that does both
		emit differenceClicked( diffItem->difference() );
		emit applyDifference( !diffItem->difference()->applied() );
	}
}

void KompareListView::renumberLines( void )
{
//	kDebug( 8104 ) << "Begin" << endl;
	unsigned int newLineNo = 1;
	if( !topLevelItemCount() ) return;
	KompareListViewItem* item = (KompareListViewItem*)topLevelItem( 0 );
	while( item ) {
//		kDebug( 8104 ) << "rtti: " << item->rtti() << endl;
		if ( item->rtti() != KompareListViewItem::Container 
		     && item->rtti() != KompareListViewItem::Blank 
		     && item->rtti() != KompareListViewItem::Hunk )
		{
//			kDebug( 8104 ) << QString::number( newLineNo ) << endl;
			item->setText( COL_LINE_NO, QString::number( newLineNo++ ) );
		}
		item = (KompareListViewItem*)itemBelow( item );
	}
}

void KompareListView::slotApplyDifference( bool apply )
{
	m_itemDict[ m_selectedDifference ]->applyDifference( apply );
	// now renumber the line column if this is the destination
	if ( !m_isSource )
		renumberLines();
}

void KompareListView::slotApplyAllDifferences( bool apply )
{
	QHash<const Diff2::Difference*, KompareListViewDiffItem*>::ConstIterator it = m_itemDict.constBegin();
	QHash<const Diff2::Difference*, KompareListViewDiffItem*>::ConstIterator end = m_itemDict.constEnd();
	for ( ; it != end; ++it )
		it.value()->applyDifference( apply );

	// now renumber the line column if this is the destination
	if ( !m_isSource )
		renumberLines();
	update();
}

void KompareListView::slotApplyDifference( const Difference* diff, bool apply )
{
	m_itemDict[ diff ]->applyDifference( apply );
	// now renumber the line column if this is the destination
	if ( !m_isSource )
		renumberLines();
}

void KompareListView::wheelEvent( QWheelEvent* e )
{
	e->ignore(); // we want the parent to catch wheel events
}

void KompareListView::resizeEvent( QResizeEvent* e )
{
	QTreeWidget::resizeEvent(e);
	emit resized();
}

KompareListViewItemDelegate::KompareListViewItemDelegate( QObject* parent )
	: QItemDelegate( parent ),
	m_item(0),
	m_column(0)
{
}

KompareListViewItemDelegate::~KompareListViewItemDelegate()
{
}

void KompareListViewItemDelegate::paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
	m_column = index.column();
	QStyleOptionViewItemV4 changedOption = option;
	if( m_column == COL_LINE_NO )
		changedOption.displayAlignment = Qt::AlignRight;
	m_item = static_cast<KompareListViewItem*>( static_cast<KompareListView*>( parent() )->itemFromIndex( index ) );
	m_item->paintCell( painter, changedOption, m_column, this );
}

QSize KompareListViewItemDelegate::sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
	KompareListViewItem* item = static_cast<KompareListViewItem*>( static_cast<KompareListView*>( parent() )->itemFromIndex( index ) );
	item->setup();
	QSize hint = QItemDelegate::sizeHint( option, index );
	return QSize( hint.width() + ITEM_MARGIN, item->height() );
}

void KompareListViewItemDelegate::paintDefault( QPainter* painter, const QStyleOptionViewItem& option, int column, KompareListViewItem* item ) const
{
	QModelIndex index = static_cast<KompareListView*>( parent() )->indexFromItem( item, column );
	QItemDelegate::paint( painter, option, index );
}

void KompareListViewItemDelegate::drawDisplayDefault( QPainter* painter, const QStyleOptionViewItem& option, const QRect& rect, const QString& text ) const
{
	QItemDelegate::drawDisplay( painter, option, rect, text );
}

void KompareListViewItemDelegate::drawDisplay( QPainter* painter, const QStyleOptionViewItem& option, const QRect& rect, const QString& text ) const
{
	m_item->paintText( painter, option, rect, text, m_column, this );
}

void KompareListViewItemDelegate::drawFocus( QPainter* /* painter */, const QStyleOptionViewItem& /* option */, const QRect& /* rect */ ) const
{
	// draw nothing
}

KompareListViewItem::KompareListViewItem( KompareListView* parent )
	: QTreeWidgetItem( parent ),
	m_scrollId( 0 ),
	m_height( 0 )
{
//	kDebug(8104) << "Created KompareListViewItem with scroll id " << m_scrollId << endl;
}

KompareListViewItem::KompareListViewItem( KompareListView* parent, KompareListViewItem* after )
	: QTreeWidgetItem( parent, after ),
	m_scrollId( after->scrollId() + after->maxHeight() ),
	m_height( 0 )
{
//	kDebug(8104) << "Created KompareListViewItem with scroll id " << m_scrollId << endl;
}

KompareListViewItem::KompareListViewItem( KompareListViewItem* parent )
	: QTreeWidgetItem( parent ),
	m_scrollId( 0 ),
	m_height( 0 )
{
}

KompareListViewItem::KompareListViewItem( KompareListViewItem* parent, KompareListViewItem* /*after*/ )
	: QTreeWidgetItem( parent ),
	m_scrollId( 0 ),
	m_height( 0 )
{
}

void KompareListViewItem::paintCell( QPainter* p, const QStyleOptionViewItem& option, int column, const KompareListViewItemDelegate* delegate )
{
	delegate->paintDefault( p, option, column, this );
}

void KompareListViewItem::paintText( QPainter* p, const QStyleOptionViewItem& option, const QRect& rect, const QString& text, int /* column */, const KompareListViewItemDelegate* delegate )
{
	delegate->drawDisplayDefault( p, option, rect, text );
}

int KompareListViewItem::height() const
{
	return m_height;
}

void KompareListViewItem::setHeight( int h )
{
	m_height = h;
	if( !m_height ) m_height++; // QTreeWidget doesn't like zero height.
}

bool KompareListViewItem::isCurrent() const
{
	return treeWidget()->currentItem() == this;
}

KompareListView* KompareListViewItem::kompareListView() const
{
	return (KompareListView*)treeWidget();
}

KompareListViewDiffItem::KompareListViewDiffItem( KompareListView* parent, Difference* difference )
	: KompareListViewItem( parent ),
	m_difference( difference ),
	m_sourceItem( 0L ),
	m_destItem( 0L )
{
	init();
}

KompareListViewDiffItem::KompareListViewDiffItem( KompareListView* parent, KompareListViewItem* after, Difference* difference )
	: KompareListViewItem( parent, after ),
	m_difference( difference ),
	m_sourceItem( 0L ),
	m_destItem( 0L )
{
	init();
}

KompareListViewDiffItem::~KompareListViewDiffItem()
{
	m_difference = 0;
	delete m_sourceItem;
	delete m_destItem;
}

void KompareListViewDiffItem::init()
{
	setExpanded( true );
	m_destItem = new KompareListViewLineContainerItem( this, false );
	m_sourceItem = new KompareListViewLineContainerItem( this, true );
	setVisibility();
}

void KompareListViewDiffItem::setup()
{
	KompareListViewItem::setup();
	setHeight( 0 );
}

void KompareListViewDiffItem::paintCell( QPainter* p, const QStyleOptionViewItem& option, int column, const KompareListViewItemDelegate* /* delegate */ )
{
	int width = option.rect.width();

	p->setRenderHint(QPainter::Antialiasing);
	p->translate(option.rect.topLeft());

	QColor bg( Qt::white ); // Always make the background white when it is not a real difference
	if ( difference()->type() == Difference::Unchanged )
	{
		if ( column == COL_LINE_NO )
		{
			bg = QColor( Qt::lightGray );
		}
	}
	else
	{
		bg = kompareListView()->settings()->colorForDifferenceType(
		          difference()->type(),
		          isCurrent(),
		          difference()->applied() );
	}

	// Paint background
	p->fillRect( 0, 0, width, height(), bg );

	// Paint darker lines around selected item
	if ( isCurrent() )
	{
		p->translate(0.5,0.5);
		p->setPen( bg.dark(135) );
		p->drawLine( 0, 0, width, 0 );
	}

	p->resetTransform();
}

void KompareListViewDiffItem::setVisibility()
{
	m_sourceItem->setHidden( !(kompareListView()->isSource() || m_difference->applied()) );
	m_destItem->setHidden( !m_sourceItem->isHidden() );
}

void KompareListViewDiffItem::applyDifference( bool apply )
{
	kDebug(8104) << "KompareListViewDiffItem::applyDifference( " << apply << " )" << endl;
	setVisibility();
	setup();
}

int KompareListViewDiffItem::maxHeight()
{
	int lines = qMax( m_difference->sourceLineCount(), m_difference->destinationLineCount() );
	if( lines == 0 )
		return BLANK_LINE_HEIGHT + 1;
	else
		return lines * treeWidget()->fontMetrics().height() + 1;
}

KompareListViewLineContainerItem::KompareListViewLineContainerItem( KompareListViewDiffItem* parent, bool isSource )
	: KompareListViewItem( parent ),
	m_blankLineItem( 0 ),
	m_isSource( isSource )
{
//	kDebug(8104) << "isSource ? " << (isSource ? " Yes!" : " No!") << endl;
	setExpanded( true );

	int lines = lineCount();
	int line = lineNumber();
//	kDebug(8104) << "LineNumber : " << lineNumber() << endl;
	if( lines == 0 ) {
		m_blankLineItem = new KompareListViewBlankLineItem( this );
		return;
	}

	for( int i = 0; i < lines; i++, line++ ) {
		m_lineItemList.append(new KompareListViewLineItem( this, line, lineAt( i ) ) );
	}
}

KompareListViewLineContainerItem::~KompareListViewLineContainerItem()
{
	delete m_blankLineItem;
	qDeleteAll( m_lineItemList );
}

void KompareListViewLineContainerItem::setup()
{
	KompareListViewItem::setup();
	setHeight( 0 );
}

void KompareListViewLineContainerItem::paintCell( QPainter* p, const QStyleOptionViewItem& option, int column, const KompareListViewItemDelegate* /* delegate */ )
{
	int width = option.rect.width();

	p->setRenderHint(QPainter::Antialiasing);
	p->translate(option.rect.topLeft());

	QColor bg( Qt::white ); // Always make the background white when it is not a real difference
	if ( diffItemParent()->difference()->type() == Difference::Unchanged )
	{
		if ( column == COL_LINE_NO )
		{
			bg = QColor( Qt::lightGray );
		}
	}
	else
	{
		bg = kompareListView()->settings()->colorForDifferenceType(
		          diffItemParent()->difference()->type(),
		          diffItemParent()->isCurrent(),
		          diffItemParent()->difference()->applied() );
	}

	// Paint background
	p->fillRect( 0, 0, width, height(), bg );

	p->resetTransform();
}

KompareListViewDiffItem* KompareListViewLineContainerItem::diffItemParent() const
{
	return (KompareListViewDiffItem*)parent();
}

int KompareListViewLineContainerItem::lineCount() const
{
	return m_isSource ? diffItemParent()->difference()->sourceLineCount() :
	                    diffItemParent()->difference()->destinationLineCount();
}

int KompareListViewLineContainerItem::lineNumber() const
{
	return m_isSource ? diffItemParent()->difference()->sourceLineNumber() :
	                    diffItemParent()->difference()->destinationLineNumber();
}

DifferenceString* KompareListViewLineContainerItem::lineAt( int i ) const
{
	return m_isSource ? diffItemParent()->difference()->sourceLineAt( i ) :
	                    diffItemParent()->difference()->destinationLineAt( i );
}

KompareListViewLineItem::KompareListViewLineItem( KompareListViewLineContainerItem* parent, int line, DifferenceString* text )
	: KompareListViewItem( parent )
{
	setText( COL_LINE_NO, QString::number( line ) );
	setText( COL_MAIN, text->string() );
	m_text = text;
}

KompareListViewLineItem::~KompareListViewLineItem()
{
	m_text = 0;
}

void KompareListViewLineItem::setup()
{
	KompareListViewItem::setup();
	setHeight( treeWidget()->fontMetrics().height() );
}

void KompareListViewLineItem::paintCell( QPainter* p, const QStyleOptionViewItem& option, int column, const KompareListViewItemDelegate* /* delegate */ )
{
	int width = option.rect.width();
	Qt::Alignment align = option.displayAlignment;

	p->setRenderHint(QPainter::Antialiasing);
	p->translate(option.rect.topLeft());

	QColor bg( Qt::white ); // Always make the background white when it is not a real difference
	if ( diffItemParent()->difference()->type() == Difference::Unchanged )
	{
		if ( column == COL_LINE_NO )
		{
			bg = QColor( Qt::lightGray );
		}
	}
	else
	{
		bg = kompareListView()->settings()->colorForDifferenceType(
		          diffItemParent()->difference()->type(),
		          diffItemParent()->isCurrent(),
		          diffItemParent()->difference()->applied() );
	}

	// Paint background
	p->fillRect( 0, 0, width, height(), bg );

	// Paint foreground
	if ( diffItemParent()->difference()->type() == Difference::Unchanged )
		p->setPen( QColor( Qt::darkGray ) ); // always make normal text gray
	else
		p->setPen( QColor( Qt::black ) ); // make text with changes black

	paintTextP( p, bg, column, width, align );

	// Paint darker lines around selected item
	if ( diffItemParent()->isCurrent() )
	{
		p->translate(0.5,0.5);
		p->setPen( bg.dark(135) );
		QTreeWidgetItem* parentItem = parent();
#if 0
		// We have to draw this in the DiffItem now because its height cannot be 0 anymore. :-(
		if ( this == parentItem->child( 0 ) )
			p->drawLine( 0, 0, width, 0 );
#endif
		if ( this == parentItem->child( parentItem->childCount() - 1 ) )
			p->drawLine( 0, height() - 1, width, height() - 1 );
	}

	p->resetTransform();
}

void KompareListViewLineItem::paintTextP( QPainter* p, const QColor& bg, int column, int width, int align )
{
	if ( column == COL_MAIN )
	{
		QString textChunk;
		int offset = ITEM_MARGIN;
		int prevValue = 0;
		int charsDrawn = 0;
		int chunkWidth;
		QBrush changeBrush( bg, Qt::Dense3Pattern );
		QBrush normalBrush( bg, Qt::SolidPattern );
		QBrush brush;

		if ( m_text->string().isEmpty() )
		{
			p->fillRect( 0, 0, width, height(), normalBrush );
			return;
		}

		p->fillRect( 0, 0, offset, height(), normalBrush );

		if ( !m_text->markerList().isEmpty() )
		{
			MarkerListConstIterator markerIt = m_text->markerList().begin();
			MarkerListConstIterator mEnd     = m_text->markerList().end();
			Marker* m = *markerIt;

			for ( ; markerIt != mEnd; ++markerIt )
			{
				m  = *markerIt;
				textChunk = m_text->string().mid( prevValue, m->offset() - prevValue );
//				kDebug(8104) << "TextChunk   = \"" << textChunk << "\"" << endl;
//				kDebug(8104) << "c->offset() = " << c->offset() << endl;
//				kDebug(8104) << "prevValue   = " << prevValue << endl;
				expandTabs(textChunk, kompareListView()->settings()->m_tabToNumberOfSpaces, charsDrawn);
				charsDrawn += textChunk.length();
				prevValue = m->offset();
				if ( m->type() == Marker::End )
				{
					QFont font( p->font() );
					font.setBold( true );
					p->setFont( font );
//					p->setPen( Qt::blue );
					brush = changeBrush;
				}
				else
				{
					QFont font( p->font() );
					font.setBold( false );
					p->setFont( font );
//					p->setPen( Qt::black );
					brush = normalBrush;
				}
				chunkWidth = p->fontMetrics().width( textChunk );
				p->fillRect( offset, 0, chunkWidth, height(), brush );
				p->drawText( offset, 0,
				             chunkWidth, height(),
				             align, textChunk );
				offset += chunkWidth;
			}
		}
		if ( prevValue < m_text->string().length() )
		{
			// Still have to draw some string without changes
			textChunk = m_text->string().mid( prevValue, qMax( 1, m_text->string().length() - prevValue ) );
			expandTabs(textChunk, kompareListView()->settings()->m_tabToNumberOfSpaces, charsDrawn);
//			kDebug(8104) << "TextChunk   = \"" << textChunk << "\"" << endl;
			QFont font( p->font() );
			font.setBold( false );
			p->setFont( font );
			chunkWidth = p->fontMetrics().width( textChunk );
			p->fillRect( offset, 0, chunkWidth, height(), normalBrush );
			p->drawText( offset, 0,
			             chunkWidth, height(),
			             align, textChunk );
			offset += chunkWidth;
		}
		p->fillRect( offset, 0, width - offset, height(), normalBrush );
	}
	else
	{
		p->fillRect( 0, 0, width, height(), bg );
		p->drawText( ITEM_MARGIN, 0,
		             width - ITEM_MARGIN, height(),
		             align, text( column ) );
	}
}

void KompareListViewLineItem::expandTabs(QString& text, int tabstop, int startPos) const
{
	int index;
	while((index = text.indexOf(QChar(9)))!= -1)
		text.replace(index, 1, QString(tabstop-((startPos+index)%4),' '));
}

KompareListViewDiffItem* KompareListViewLineItem::diffItemParent() const
{
	KompareListViewLineContainerItem* p = (KompareListViewLineContainerItem*)parent();
	return p->diffItemParent();
}

KompareListViewBlankLineItem::KompareListViewBlankLineItem( KompareListViewLineContainerItem* parent )
	: KompareListViewLineItem( parent, 0, new DifferenceString() )
{
}

void KompareListViewBlankLineItem::setup()
{
	KompareListViewLineItem::setup();
	setHeight( BLANK_LINE_HEIGHT );
}

void KompareListViewBlankLineItem::paintText( QPainter* p, const QStyleOptionViewItem& option, const QRect& rect, const QString& /* text */, int column, const KompareListViewItemDelegate* /* delegate */ )
{
	if ( column == COL_MAIN )
	{
		QBrush normalBrush( option.palette.color( QPalette::Base ), Qt::SolidPattern );
		p->fillRect( rect.left(), rect.top(), rect.width(), height(), normalBrush );
	}
}

KompareListViewHunkItem::KompareListViewHunkItem( KompareListView* parent, DiffHunk* hunk, bool zeroHeight )
	: KompareListViewItem( parent ),
	m_zeroHeight( zeroHeight ),
	m_hunk( hunk )
{
	setFlags( flags() & ~Qt::ItemIsSelectable );
}

KompareListViewHunkItem::KompareListViewHunkItem( KompareListView* parent, KompareListViewItem* after, DiffHunk* hunk,  bool zeroHeight )
	: KompareListViewItem( parent, after ),
	m_zeroHeight( zeroHeight ),
	m_hunk( hunk )
{
	setFlags( flags() & ~Qt::ItemIsSelectable );
}

KompareListViewHunkItem::~KompareListViewHunkItem()
{
	m_hunk = 0;
}

int KompareListViewHunkItem::maxHeight()
{
	if( m_zeroHeight ) {
		return 1; // minimum height forced to 1 because QTreeWidget doesn't like zero height
	} else if( m_hunk->function().isEmpty() ) {
		return HUNK_LINE_HEIGHT;
	} else {
		return treeWidget()->fontMetrics().height();
	}
}

void KompareListViewHunkItem::setup()
{
	KompareListViewItem::setup();

	setHeight( maxHeight() );
}

void KompareListViewHunkItem::paintCell( QPainter* p, const QStyleOptionViewItem& option, int column, const KompareListViewItemDelegate* /* delegate */ )
{
	int x = option.rect.left();
	int y = option.rect.top();
	int width = option.rect.width();
	Qt::Alignment align = option.displayAlignment;

	if( column == COL_MAIN && m_zeroHeight )
		p->fillRect( x, y, width, height(), QColor( Qt::white ) ); // fill the 1 pixel minimum height with white
	else
		p->fillRect( x, y, width, height(), QColor( Qt::lightGray ) ); // Hunk headers should be lightgray 
	p->setPen( QColor( Qt::black ) ); // Text color in hunk should be black
	if( column == COL_MAIN ) {
		p->drawText( x + ITEM_MARGIN, y, width - ITEM_MARGIN, height(),
		             align, m_hunk->function() );
	}
}

#include "komparelistview.moc"
