// 移植自 https://github.com/CommunityToolkit/WindowsCommunityToolkit/tree/77b009ddf591b78dfc5bad0088c99ce35406170b/Microsoft.Toolkit.Uwp.UI.Controls.Primitives/WrapPanel

#include "pch.h"
#include "WrapPanel.h"
#if __has_include("WrapPanel.g.cpp")
#include "WrapPanel.g.cpp"
#endif

using namespace winrt;
using namespace Windows::UI::Xaml::Controls;

namespace winrt::Magpie::implementation {

void WrapPanel::HorizontalSpacing(double value) {
	if (_horizontalSpacing == value) {
		return;
	}

	_horizontalSpacing = value;
	RaisePropertyChanged(L"HorizontalSpacing");

	_UpdateLayout();
}

void WrapPanel::VerticalSpacing(double value) {
	if (_verticalSpacing == value) {
		return;
	}

	_verticalSpacing = value;
	RaisePropertyChanged(L"VerticalSpacing");

	_UpdateLayout();
}

void WrapPanel::Orientation(enum Orientation value) {
	if (_orientation == value) {
		return;
	}

	_orientation = value;
	RaisePropertyChanged(L"Orientation");

	_UpdateLayout();
}

void WrapPanel::Padding(const Thickness& value) {
	if (_padding == value) {
		return;
	}

	_padding = value;
	RaisePropertyChanged(L"Padding");

	_UpdateLayout();
}

void WrapPanel::StretchChild(Magpie::StretchChild value) {
	if (_stretchChild == value) {
		return;
	}

	_stretchChild = value;
	RaisePropertyChanged(L"StretchChild");

	_UpdateLayout();
}

Size WrapPanel::MeasureOverride(const Size& availableSize) {
	const Thickness padding = Padding();
	const Size childAvailableSize{
		availableSize.Width - (float)padding.Left - (float)padding.Right,
		availableSize.Height - (float)padding.Top - (float)padding.Bottom
	};
	for (const UIElement& item : Children()) {
		item.Measure(childAvailableSize);
	}

	return _UpdateRows(availableSize);
}

Size WrapPanel::ArrangeOverride(Size finalSize) {
	enum Orientation orientation = Orientation();
	Size desiredSize = DesiredSize();

	if ((orientation == Orientation::Horizontal && finalSize.Width < desiredSize.Width) ||
		(orientation == Orientation::Vertical && finalSize.Height < desiredSize.Height)) {
		// 没收到 DesiredSize，重新计算布局
		_UpdateRows(finalSize);
	}

	if (_rows.empty()) {
		return finalSize;
	}

	UIElementCollection children = Children();

	// 更新布局
	uint32_t childIndex = 0;
	for (const Row& row : _rows) {
		for (const UvRect& rect : row.childrenRects) {
			UIElement child = children.GetAt(childIndex++);
			while (child.Visibility() == Visibility::Collapsed) {
				// _rows 不包含不可见的子项
				child = children.GetAt(childIndex++);
			}

			UvRect arrangeRect{
				rect.position,
				UvMeasure(rect.size.u, row.size.v),
			};
			child.Arrange(arrangeRect.ToRect(orientation));
		}
	}

	return finalSize;
}

void WrapPanel::_UpdateLayout() const {
	InvalidateMeasure();
	InvalidateArrange();
}

Size WrapPanel::_UpdateRows(Size availableSize) {
	_rows.clear();

	const enum Orientation orientation = Orientation();
	const Thickness padding = Padding();
	UIElementCollection children = Children();

	UvMeasure paddingStart(orientation, (float)padding.Left, (float)padding.Top);
	UvMeasure paddingEnd(orientation, (float)padding.Right, (float)padding.Bottom);

	if (children.Size() > 0) {
		const UvMeasure parentMeasure(orientation, availableSize.Width, availableSize.Height);
		const UvMeasure spacingMeasure(orientation, (float)HorizontalSpacing(), (float)VerticalSpacing());
		UvMeasure position(orientation, (float)padding.Left, (float)padding.Top);

		Row currentRow;
		UvMeasure finalMeasure(orientation, 0.0f, 0.0f);

		const uint32_t count = children.Size();
		for (uint32_t i = 0; i < count; ++i) {
			UIElement const& item = children.GetAt(i);

			if (item.Visibility() == Visibility::Collapsed) {
				// 不可见的子项不添加间距
				continue;
			}

			UvMeasure desiredMeasure(orientation, item.DesiredSize());
			if (desiredMeasure.u + position.u + paddingEnd.u > parentMeasure.u) {
				finalMeasure.u = std::max(finalMeasure.u, position.u - spacingMeasure.u);

				// 下一行
				position.u = paddingStart.u;
				position.v += currentRow.size.v + spacingMeasure.v;

				_rows.push_back(std::move(currentRow));
				currentRow = {};
			}

			if (i == count - 1 && StretchChild() == StretchChild::Last && !std::isinf(parentMeasure.u)) {
				// 让最后一个子项填满剩余空间，剩余空间必须有限才有意义
				desiredMeasure.u = parentMeasure.u - position.u;
			}

			currentRow.Add(position, desiredMeasure);

			// 下一个子项的位置
			position.u += desiredMeasure.u + spacingMeasure.u;
		}

		// 添加最后一行
		if (!currentRow.childrenRects.empty()) {
			finalMeasure.u = std::max(finalMeasure.u, position.u - spacingMeasure.u);
			_rows.push_back(std::move(currentRow));
		}

		if (!_rows.empty()) {
			// 计算 finalMeasure 的 v 分量
			UvRect lastRowRect = _rows.back().Rect();
			finalMeasure.v = lastRowRect.position.v + lastRowRect.size.v;

			finalMeasure.Add(paddingEnd);
			return finalMeasure.ToSize(orientation);
		}
	}
	
	return UvMeasure(paddingStart.u + paddingEnd.u, paddingStart.v + paddingEnd.v).ToSize(orientation);
}

}
