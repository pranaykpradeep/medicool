import 'package:flutter_test/flutter_test.dart';
import 'package:flutter/material.dart';
import 'package:medicool_app/main.dart';

void main() {
  testWidgets('App smoke test', (WidgetTester tester) async {
    await tester.pumpWidget(const MediCoolApp());
    expect(find.byType(MaterialApp), findsOneWidget);
  });
}
